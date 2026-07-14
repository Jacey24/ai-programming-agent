#include "TaskController.h"

#include "application/ToolSystem.h"

#include "domain/agent/AgentOrchestrator.h"
#include "facade/DataAccessFacade.h"
#include "facade/SSEGateway.h"

#include <algorithm>
#include <exception>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;

std::string json_escape(const std::string &value) {
  std::ostringstream escaped;
  for (const char ch : value) {
    switch (ch) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      escaped << ch;
    }
  }
  return escaped.str();
}

std::string http_response(const std::string &body,
                          const std::string &status = "200 OK") {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: application/json; charset=utf-8\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "Connection: close\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n"
           << body;
  return response.str();
}

std::string request_body(const std::string &request) {
  const std::size_t body_pos = request.find("\r\n\r\n");
  if (body_pos == std::string::npos)
    return "";
  return request.substr(body_pos + 4);
}

std::string extract_path_segment(const std::string &request,
                                 const std::string &prefix) {
  const std::size_t request_line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, request_line_end);
  const std::size_t method_end = request_line.find(' ');
  if (method_end == std::string::npos)
    return "";
  const std::size_t path_start = method_end + 1;
  const std::size_t prefix_pos = request_line.find(prefix, path_start);
  if (prefix_pos == std::string::npos)
    return "";
  const std::size_t segment_start = prefix_pos + prefix.size();
  const std::size_t segment_end =
      request_line.find_first_of("? ", segment_start);
  if (segment_end == std::string::npos) {
    return request_line.substr(segment_start);
  }
  return request_line.substr(segment_start, segment_end - segment_start);
}

int extract_query_int(const std::string &request, const std::string &key,
                      int fallback) {
  const std::size_t request_line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, request_line_end);
  const std::string marker = key + "=";
  const std::size_t marker_pos = request_line.find(marker);
  if (marker_pos == std::string::npos) {
    return fallback;
  }
  const std::size_t value_start = marker_pos + marker.size();
  const std::size_t value_end = request_line.find_first_of("& ", value_start);
  try {
    return std::stoi(request_line.substr(value_start, value_end - value_start));
  } catch (...) {
    return fallback;
  }
}

} // namespace

namespace codepilot {

TaskController::TaskController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

// ★ v2: 兼容层 — 解析 global_id，支持旧字段 session_id
static std::string resolveGlobalIdFromBody(const json &body) {
  // 优先新字段 global_id
  if (body.contains("global_id") && body["global_id"].is_string()) {
    return body["global_id"].get<std::string>();
  }
  // 降级到旧字段 session_id
  if (body.contains("session_id") && body["session_id"].is_string()) {
    return body["session_id"].get<std::string>();
  }
  // 如果都没有，返回空让 Orchestrator 使用默认 Global
  return "";
}

std::string TaskController::createTask(const std::string &request) {
  const std::string body_content = request_body(request);
  json body = json::parse(body_content, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  const std::string global_id = resolveGlobalIdFromBody(body);
  const std::string workspace_id = [&]() {
    if (!body.contains("workspace_id") || !body["workspace_id"].is_string())
      return std::string();
    return body["workspace_id"].get<std::string>();
  }();
  const std::string input = [&]() {
    if (!body.contains("input") || !body["input"].is_string())
      return std::string();
    return body["input"].get<std::string>();
  }();
  if (workspace_id.empty() || input.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id and input are required"}})",
        "400 Bad Request");
  }

  // 创建 DB 任务记录
  TaskRecord task;
  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    std::string effectiveGlobalId = global_id;
    if (effectiveGlobalId.empty()) {
      effectiveGlobalId = facade.ensureDefaultGlobal();
    }
    task = facade.createTask(effectiveGlobalId, workspace_id, input);
    facade.updateTaskStatus(task.id, "running", task.plan, task.current_step);
    const auto refreshed = facade.getTask(task.id);
    if (refreshed) {
      task = *refreshed;
    }
    EventData created = EventData::Create(task.id, EventType::TaskCreated,
                                          "任务已创建，正在启动 Agent",
                                          json{{"status", "running"}});
    if (SSEGateway::getInstance().isInitialized()) {
      SSEGateway::getInstance().pushStatus(task.id, created.content,
                                           EventType::TaskCreated,
                                           created.metadata.dump());
    } else {
      facade.saveEvent(created.id, created.taskId, created.typeToString(),
                       created.content, created.metadata.dump());
    }
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  auto &orch = AgentOrchestrator::getInstance();
  if (!orch.isReady()) {
    return http_response(
        R"({"success":false,"error":{"code":"ORCHESTRATOR_ERROR","message":"AgentOrchestrator not ready"}})",
        "500 Internal Server Error");
  }
  try {
    orch.startTask(task.id, global_id, workspace_id, input);
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"TASK_CREATE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                << R"(","global_id":")" << json_escape(task.session_id)
                << R"(","workspace_id":")" << json_escape(task.workspace_id)
                << R"(","goal":")" << json_escape(task.goal)
                << R"(","status":")" << json_escape(task.status) << R"(")";
  if (!task.plan.empty()) {
    response_body << R"(,"plan":)" << task.plan;
  }
  if (!task.current_step.empty()) {
    response_body << R"(,"current_step":")" << json_escape(task.current_step)
                  << R"(")";
  }
  response_body << R"(,"created_at":")" << json_escape(task.created_at)
                << R"(","updated_at":")" << json_escape(task.updated_at)
                << R"("}})";
  return http_response(response_body.str());
}

// ★ v2: continueTask 保留兼容 — 重定向到 createTask
std::string TaskController::continueTask(const std::string &request) {
  const std::string body_content = request_body(request);
  json body = json::parse(body_content, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  // 从 parent_task_id 提取 global_id（兼容旧行为）
  const std::string parent_task_id = [&]() {
    if (!body.contains("parent_task_id") || !body["parent_task_id"].is_string())
      return std::string();
    return body["parent_task_id"].get<std::string>();
  }();

  // 如果传了 parent_task_id，查其所属 Global
  std::string global_id = resolveGlobalIdFromBody(body);
  if (global_id.empty() && !parent_task_id.empty()) {
    auto parentTask = DataAccessFacade::getInstance().getTask(parent_task_id);
    if (parentTask) {
      global_id =
          parentTask->session_id; // TaskRecord.session_id 现在存的是 global_id
    }
  }

  const std::string workspace_id = [&]() {
    if (!body.contains("workspace_id") || !body["workspace_id"].is_string())
      return std::string();
    return body["workspace_id"].get<std::string>();
  }();
  const std::string input = [&]() {
    if (!body.contains("input") || !body["input"].is_string())
      return std::string();
    return body["input"].get<std::string>();
  }();

  if (workspace_id.empty() || input.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id and input are required"}})",
        "400 Bad Request");
  }

  // 创建 DB 任务记录
  TaskRecord task;
  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    std::string effectiveGlobalId = global_id;
    if (effectiveGlobalId.empty()) {
      effectiveGlobalId = facade.ensureDefaultGlobal();
    }
    task = facade.createTask(effectiveGlobalId, workspace_id, input);
    facade.updateTaskStatus(task.id, "running", task.plan, task.current_step);

    EventData created = EventData::Create(task.id, EventType::TaskCreated,
                                          "任务已创建，正在启动 Agent",
                                          json{{"status", "running"}});
    if (SSEGateway::getInstance().isInitialized()) {
      SSEGateway::getInstance().pushStatus(task.id, created.content,
                                           EventType::TaskCreated,
                                           created.metadata.dump());
    } else {
      facade.saveEvent(created.id, created.taskId, created.typeToString(),
                       created.content, created.metadata.dump());
    }
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  auto &orch = AgentOrchestrator::getInstance();
  if (!orch.isReady()) {
    return http_response(
        R"({"success":false,"error":{"code":"ORCHESTRATOR_ERROR","message":"AgentOrchestrator not ready"}})",
        "500 Internal Server Error");
  }

  try {
    orch.startTask(task.id, global_id, workspace_id, input);
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"TASK_CREATE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                << R"(","global_id":")" << json_escape(task.session_id)
                << R"(","workspace_id":")" << json_escape(workspace_id)
                << R"(","goal":")" << json_escape(input)
                << R"(","status":"running"}})";
  return http_response(response_body.str());
}

std::string TaskController::listTasks(const std::string &request) {
  std::vector<TaskRecord> tasks;
  try {
    if (DataAccessFacade::getInstance().isInitialized()) {
      tasks = DataAccessFacade::getInstance().listRecentTasks(
          extract_query_int(request, "page_size", 20));
    }
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  std::ostringstream body;
  body << R"({"success":true,"data":{"items":[)";
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    const auto &t = tasks[i];
    if (i > 0) {
      body << ",";
    }
    body << R"({"id":")" << json_escape(t.id) << R"(","global_id":")"
         << json_escape(t.session_id) << R"(","workspace_id":")"
         << json_escape(t.workspace_id) << R"(","goal":")"
         << json_escape(t.goal) << R"(","status":")" << json_escape(t.status)
         << R"(","created_at":")" << json_escape(t.created_at)
         << R"(","updated_at":")" << json_escape(t.updated_at) << R"("})";
  }
  body << "]}}";
  return http_response(body.str());
}

std::string TaskController::getTask(const std::string &request) {
  const std::string task_id = extract_path_segment(request, "/api/v1/tasks/");
  if (task_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
        "400 Bad Request");
  }

  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    const auto task = DataAccessFacade::getInstance().getTask(task_id);
    if (!task) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
          "404 Not Found");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(task->id)
         << R"(","global_id":")" << json_escape(task->session_id)
         << R"(","workspace_id":")" << json_escape(task->workspace_id)
         << R"(","goal":")" << json_escape(task->goal) << R"(","status":")"
         << json_escape(task->status) << R"(")";
    if (!task->plan.empty()) {
      body << R"(,"plan":)" << task->plan;
    }
    if (!task->current_step.empty()) {
      body << R"(,"current_step":")" << json_escape(task->current_step)
           << R"(")";
    }
    body << R"(,"created_at":")" << json_escape(task->created_at)
         << R"(","updated_at":")" << json_escape(task->updated_at) << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string TaskController::cancelTask(const std::string &request) {
  const std::string full_segment =
      extract_path_segment(request, "/api/v1/tasks/");
  const std::string cancel_suffix = "/cancel";
  if (full_segment.size() <= cancel_suffix.size() ||
      full_segment.compare(full_segment.size() - cancel_suffix.size(),
                           cancel_suffix.size(), cancel_suffix) != 0) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /cancel"}})",
        "400 Bad Request");
  }
  const std::string task_id =
      full_segment.substr(0, full_segment.size() - cancel_suffix.size());
  if (task_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
        "400 Bad Request");
  }

  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    const auto task = DataAccessFacade::getInstance().getTask(task_id);
    if (!task) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
          "404 Not Found");
    }

    AgentOrchestrator::getInstance().cancelTask(task_id);
    DataAccessFacade::getInstance().updateTaskStatus(task_id, "cancelled", "",
                                                     "");

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(task->id)
         << R"(","global_id":")" << json_escape(task->session_id)
         << R"(","workspace_id":")" << json_escape(task->workspace_id)
         << R"(","goal":")" << json_escape(task->goal) << R"(","status":")"
         << json_escape("cancelled") << R"(")";
    if (!task->plan.empty()) {
      body << R"(,"plan":)" << task->plan;
    }
    if (!task->current_step.empty()) {
      body << R"(,"current_step":")" << json_escape(task->current_step)
           << R"(")";
    }
    body << R"(,"created_at":")" << json_escape(task->created_at)
         << R"(","updated_at":")" << json_escape(task->updated_at) << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string TaskController::listToolCalls(const std::string &request) {
  const std::string full_segment =
      extract_path_segment(request, "/api/v1/tasks/");
  const std::string suffix = "/tool-calls";
  if (full_segment.size() <= suffix.size() ||
      full_segment.compare(full_segment.size() - suffix.size(), suffix.size(),
                           suffix) != 0) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /tool-calls"}})",
        "400 Bad Request");
  }
  const std::string task_id =
      full_segment.substr(0, full_segment.size() - suffix.size());
  if (task_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
        "400 Bad Request");
  }

  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    const std::vector<ToolCallRecord> calls =
        DataAccessFacade::getInstance().getToolCallsByTaskId(task_id);

    std::ostringstream body;
    body << R"({"success":true,"data":{"task_id":")" << json_escape(task_id)
         << R"(","items":[)";
    for (std::size_t i = 0; i < calls.size(); ++i) {
      const auto &call = calls[i];
      if (i > 0) {
        body << ",";
      }
      body << R"({"id":")" << json_escape(call.id) << R"(","task_id":")"
           << json_escape(call.task_id) << R"(","tool_name":")"
           << json_escape(call.tool_name) << R"(","arguments":")"
           << json_escape(call.arguments) << R"(","success":)"
           << (call.success ? "true" : "false") << R"(,"result":")"
           << json_escape(call.result) << R"(","exit_code":)" << call.exit_code
           << R"(,"created_at":")" << json_escape(call.created_at) << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string TaskController::listEventHistory(const std::string &request) {
  const std::string full_segment =
      extract_path_segment(request, "/api/v1/tasks/");
  const std::string suffix = "/events/history";
  if (full_segment.size() <= suffix.size() ||
      full_segment.compare(full_segment.size() - suffix.size(), suffix.size(),
                           suffix) != 0) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /events/history"}})",
        "400 Bad Request");
  }
  const std::string task_id =
      full_segment.substr(0, full_segment.size() - suffix.size());
  if (task_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
        "400 Bad Request");
  }

  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    const std::vector<EventRecord> events =
        DataAccessFacade::getInstance().getEventsByTaskId(task_id);

    std::ostringstream body;
    body << R"({"success":true,"data":{"task_id":")" << json_escape(task_id)
         << R"(","items":[)";
    for (std::size_t i = 0; i < events.size(); ++i) {
      const auto &event = events[i];
      if (i > 0) {
        body << ",";
      }
      body << R"({"id":")" << json_escape(event.id) << R"(","task_id":")"
           << json_escape(event.task_id) << R"(","type":")"
           << json_escape(event.type) << R"(","content":")"
           << json_escape(event.content) << R"(","metadata":)";
      json metadata = json::parse(event.metadata, nullptr, false);
      if (metadata.is_discarded()) {
        metadata = json::object();
      }
      body << metadata.dump() << R"(,"created_at":")"
           << json_escape(event.created_at) << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string TaskController::deleteTask(const std::string &request) {
  const std::string task_id = extract_path_segment(request, "/api/v1/tasks/");
  if (task_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
        "400 Bad Request");
  }

  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    auto task = facade.getTask(task_id);
    if (!task) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
          "404 Not Found");
    }

    if (task->status == "running") {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_RUNNING","message":"cannot delete a running task"}})",
          "409 Conflict");
    }

    facade.deleteTaskCascade(task_id);

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(task_id)
         << R"(","deleted":true}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string TaskController::listActiveTasks() {
  auto &orch = AgentOrchestrator::getInstance();
  std::vector<ActiveTaskState> active = orch.activeTasks();

  std::ostringstream body;
  body << R"({"success":true,"data":{"items":[)";
  for (std::size_t i = 0; i < active.size(); ++i) {
    const auto &state = active[i];
    if (i > 0) {
      body << ",";
    }
    body << R"({"task_id":")" << json_escape(state.taskId)
         << R"(","global_id":")" << json_escape(state.globalId)
         << R"(","workspace_id":")" << json_escape(state.workspaceId)
         << R"(","goal":")" << json_escape(state.goal)
         << R"(","current_expert":")" << json_escape(state.currentExpert)
         << R"(","current_stage":")" << json_escape(state.currentStage)
         << R"(","expert_chain":[)";
    for (std::size_t j = 0; j < state.expertChain.size(); ++j) {
      if (j > 0) {
        body << ",";
      }
      body << R"(")" << json_escape(state.expertChain[j]) << R"(")";
    }
    body << R"(],"status":")" << json_escape(state.status) << R"("})";
  }
  body << "]}}";
  return http_response(body.str());
}

} // namespace codepilot
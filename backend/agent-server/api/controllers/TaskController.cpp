#include "TaskController.h"

#include "application/AgentService.h"
#include "application/LogService.h"
#include "application/ToolSystem.h"

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

std::string TaskController::createTask(const std::string &request) {
  const std::string body_content = request_body(request);
  json body = json::parse(body_content, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  const std::string session_id = [&]() {
    if (!body.contains("session_id") || !body["session_id"].is_string())
      return std::string();
    return body["session_id"].get<std::string>();
  }();
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
  if (session_id.empty() || workspace_id.empty() || input.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id, workspace_id and input are required"}})",
        "400 Bad Request");
  }
  const json options = body.contains("options") && body["options"].is_object()
                           ? body["options"]
                           : json::object();
  TaskRunOptions runOptions;
  const std::string executionMode = options.value("execution_mode", "auto");
  runOptions.mode = executionMode == "answer" ? ExecutionMode::DirectAnswer
                    : executionMode == "workspace"
                        ? ExecutionMode::WorkspaceAgent
                        : ExecutionMode::Auto;
  runOptions.autoRunSafeCommands =
      options.value("auto_run_safe_commands", true);
  runOptions.requireFileWritePermission =
      options.value("require_permission_for_file_write", true);
  runOptions.maxSteps = std::clamp(options.value("max_steps", 6), 1, 20);
  runOptions.maxRoundsPerStep =
      std::clamp(options.value("max_rounds_per_step", 3), 1, 6);

  // 第 7 点优化：通过 DataAccessFacade 创建任务，不再手动管理 sqlite3 连接
  TaskRecord task;
  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    task = facade.createTask(session_id, workspace_id, input);
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

  // 后台异步执行 Agent（第 7 点优化：后台线程也通过 DataAccessFacade 操作，
  // 不再需要独立的 worker_db 连接）
  const std::string task_id = task.id;
  std::thread([task_id, session_id, workspace_id, input, runOptions]() {
    try {
      AgentService agent_service;
      const AgentResult agent_result = agent_service.runTask(
          task_id, session_id, workspace_id, input, runOptions);
      if (DataAccessFacade::getInstance().isInitialized()) {
        auto &facade = DataAccessFacade::getInstance();
        facade.updateTaskStatus(
            task_id,
            agent_result.status.empty() ? "completed" : agent_result.status,
            agent_result.planJson, agent_result.currentStep);
        for (const auto &entry : agent_result.logs) {
          facade.appendLog(task_id,
                           entry.size() > 2 && entry.front() == '['
                               ? entry.substr(1, entry.find(']') - 1)
                               : "agent",
                           entry);
        }
      }
    } catch (const std::exception &) {
      if (DataAccessFacade::getInstance().isInitialized()) {
        DataAccessFacade::getInstance().updateTaskStatus(task_id, "failed", "",
                                                         "");
      }
    }
  }).detach();

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                << R"(","session_id":")" << json_escape(task.session_id)
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
    body << R"({"id":")" << json_escape(t.id) << R"(","session_id":")"
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
         << R"(","session_id":")" << json_escape(task->session_id)
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
    DataAccessFacade::getInstance().updateTaskStatus(task_id, "cancelled", "",
                                                     "");

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(task->id)
         << R"(","session_id":")" << json_escape(task->session_id)
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
    const std::string msg = error.what();
    if (msg.find("not found") != std::string::npos) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":")" +
              json_escape(msg) + R"("}})",
          "404 Not Found");
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(msg) + R"("}})",
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

  // 第 7 点优化：改为 DataAccessFacade 查询
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

  // 第 7 点优化：改为 DataAccessFacade 查询
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

} // namespace codepilot
#include "TaskController.h"

#include "application/ToolSystem.h"
#include "common/logging/Logger.h"

#include "domain/agent/AgentOrchestrator.h"
#include "domain/agent/TaskRunOptions.h"
#include "facade/DataAccessFacade.h"
#include "facade/SSEGateway.h"
#include "infrastructure/filesystem/WorkspaceManager.h"

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

static std::string extractStringField(const json &body, const char *field) {
  if (body.contains(field) && body[field].is_string()) {
    return body[field].get<std::string>();
  }
  return "";
}

static std::string parseTaskRunOptions(const json &body,
                                       TaskRunOptions &options) {
  if (!body.contains("options")) {
    return "";
  }

  const json &rawOptions = body["options"];
  if (!rawOptions.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_OPTIONS","message":"options must be an object"}})",
        "400 Bad Request");
  }

  if (rawOptions.contains("execution_mode")) {
    const json &rawExecutionMode = rawOptions["execution_mode"];
    if (!rawExecutionMode.is_string()) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_EXECUTION_MODE","message":"options.execution_mode must be one of: auto, answer, workspace"}})",
          "400 Bad Request");
    }

    const std::string executionMode = rawExecutionMode.get<std::string>();
    if (executionMode == "auto") {
      options.mode = ExecutionMode::Auto;
    } else if (executionMode == "answer") {
      options.mode = ExecutionMode::DirectAnswer;
    } else if (executionMode == "workspace") {
      options.mode = ExecutionMode::WorkspaceAgent;
    } else {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_EXECUTION_MODE","message":"options.execution_mode must be one of: auto, answer, workspace"}})",
          "400 Bad Request");
    }
  }

  if (rawOptions.contains("auto_run_safe_commands")) {
    const json &rawAutoRunSafeCommands = rawOptions["auto_run_safe_commands"];
    if (!rawAutoRunSafeCommands.is_boolean()) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_AUTO_RUN_SAFE_COMMANDS","message":"options.auto_run_safe_commands must be a boolean"}})",
          "400 Bad Request");
    }
    options.autoRunSafeCommands = rawAutoRunSafeCommands.get<bool>();
  }

  if (rawOptions.contains("require_permission_for_file_write")) {
    const json &rawFileWritePermission =
        rawOptions["require_permission_for_file_write"];
    if (!rawFileWritePermission.is_boolean()) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_FILE_WRITE_PERMISSION_OPTION","message":"options.require_permission_for_file_write must be a boolean"}})",
          "400 Bad Request");
    }
    options.requireFileWritePermission = rawFileWritePermission.get<bool>();
  }

  if (rawOptions.contains("max_steps")) {
    const json &rawMaxSteps = rawOptions["max_steps"];
    if (!rawMaxSteps.is_number_integer()) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_MAX_STEPS","message":"options.max_steps must be an integer between 1 and 20"}})",
          "400 Bad Request");
    }

    try {
      options.maxSteps = rawMaxSteps.get<int>();
    } catch (const std::exception &) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_MAX_STEPS","message":"options.max_steps must be an integer between 1 and 20"}})",
          "400 Bad Request");
    }

    if (options.maxSteps < 1 || options.maxSteps > 20) {
      return http_response(
          R"({"success":false,"error":{"code":"INVALID_MAX_STEPS","message":"options.max_steps must be between 1 and 20"}})",
          "400 Bad Request");
    }
  }

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

  const std::string session_id = extractStringField(body, "session_id");
  const std::string global_id = extractStringField(body, "global_id");
  const std::string workspace_id = extractStringField(body, "workspace_id");
  const std::string input = extractStringField(body, "input");
  TaskRunOptions options;
  if (const std::string error = parseTaskRunOptions(body, options);
      !error.empty()) {
    return error;
  }
  if (session_id.empty() || workspace_id.empty() || input.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id, workspace_id and input are required"}})",
        "400 Bad Request");
  }

  std::string effectiveGlobalId;
  TaskRecord task;
  MessageRecord userMessage;
  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    const auto workspace = facade.getWorkspace(workspace_id);
    if (!workspace) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
    }
    const auto session = facade.getSession(session_id);
    if (!session) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
          "404 Not Found");
    }
    if (session->workspace_id != workspace_id) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_WORKSPACE_MISMATCH","message":"session does not belong to workspace"}})",
          "409 Conflict");
    }
    WorkspaceManager::getInstance().getOrCreate(workspace->id, workspace->path);

    if (!global_id.empty()) {
      if (!facade.getGlobal(global_id)) {
        return http_response(
            R"({"success":false,"error":{"code":"GLOBAL_NOT_FOUND","message":"global not found"}})",
            "404 Not Found");
      }
      effectiveGlobalId = global_id;
    } else {
      try {
        effectiveGlobalId = facade.ensureDefaultGlobal();
      } catch (const std::exception &error) {
        return http_response(
            R"({"success":false,"error":{"code":"DEFAULT_GLOBAL_ERROR","message":")" +
                json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
      }
      if (effectiveGlobalId.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"DEFAULT_GLOBAL_ERROR","message":"default global is unavailable"}})",
            "500 Internal Server Error");
      }
    }
    auto creation = facade.createTaskWithUserMessage(
        session_id, effectiveGlobalId, workspace_id, input);
    task = creation.task;
    userMessage = creation.user_message;
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
  if (!orch.isReady(options.mode)) {
    return http_response(
        R"({"success":false,"error":{"code":"ORCHESTRATOR_ERROR","message":"AgentOrchestrator not ready"}})",
        "500 Internal Server Error");
  }
  try {
    orch.startTask(task.id, effectiveGlobalId, workspace_id, input, options);
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"TASK_CREATE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                << R"(","session_id":")" << json_escape(task.session_id)
                << R"(","global_id":")" << json_escape(task.global_id)
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
                << R"(","user_message":{"id":")"
                << json_escape(userMessage.id) << R"(","session_id":")"
                << json_escape(userMessage.session_id) << R"(","task_id":")"
                << json_escape(task.id)
                << R"(","role":"user","message_type":"normal","content":")"
                << json_escape(userMessage.content) << R"(","sequence_no":)"
                << userMessage.sequence_no << R"(,"source_event_id":null,"created_at":")"
                << json_escape(userMessage.created_at) << R"("}}})";
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

  const std::string parent_task_id = extractStringField(body, "parent_task_id");
  std::string session_id = extractStringField(body, "session_id");

  std::string global_id = extractStringField(body, "global_id");

  const std::string workspace_id = extractStringField(body, "workspace_id");
  const std::string input = extractStringField(body, "input");
  TaskRunOptions options;
  if (const std::string error = parseTaskRunOptions(body, options);
      !error.empty()) {
    return error;
  }

  if (workspace_id.empty() || input.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id and input are required"}})",
        "400 Bad Request");
  }

  std::string effectiveGlobalId;
  TaskRecord task;
  MessageRecord userMessage;
  try {
    if (!DataAccessFacade::getInstance().isInitialized()) {
      return http_response(
          R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
          "500 Internal Server Error");
    }
    auto &facade = DataAccessFacade::getInstance();
    const auto workspace = facade.getWorkspace(workspace_id);
    if (!workspace) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
    }
    WorkspaceManager::getInstance().getOrCreate(workspace->id, workspace->path);
    if (!parent_task_id.empty()) {
      const auto parentTask = facade.getTask(parent_task_id);
      if (parentTask) {
        if (session_id.empty()) {
          session_id = parentTask->session_id;
        }
        if (global_id.empty()) {
          global_id = parentTask->global_id;
        }
      }
    }

    if (session_id.empty()) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_ID_REQUIRED","message":"session_id is required"}})",
          "400 Bad Request");
    }
    const auto session = facade.getSession(session_id);
    if (!session) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
          "404 Not Found");
    }
    if (session->workspace_id != workspace_id) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_WORKSPACE_MISMATCH","message":"session does not belong to workspace"}})",
          "409 Conflict");
    }

    if (!global_id.empty()) {
      if (!facade.getGlobal(global_id)) {
        return http_response(
            R"({"success":false,"error":{"code":"GLOBAL_NOT_FOUND","message":"global not found"}})",
            "404 Not Found");
      }
      effectiveGlobalId = global_id;
    } else {
      try {
        effectiveGlobalId = facade.ensureDefaultGlobal();
      } catch (const std::exception &error) {
        return http_response(
            R"({"success":false,"error":{"code":"DEFAULT_GLOBAL_ERROR","message":")" +
                json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
      }
      if (effectiveGlobalId.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"DEFAULT_GLOBAL_ERROR","message":"default global is unavailable"}})",
            "500 Internal Server Error");
      }
    }
    auto creation = facade.createTaskWithUserMessage(
        session_id, effectiveGlobalId, workspace_id, input);
    task = creation.task;
    userMessage = creation.user_message;
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
  if (!orch.isReady(options.mode)) {
    return http_response(
        R"({"success":false,"error":{"code":"ORCHESTRATOR_ERROR","message":"AgentOrchestrator not ready"}})",
        "500 Internal Server Error");
  }

  try {
    orch.startTask(task.id, effectiveGlobalId, workspace_id, input, options);
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"TASK_CREATE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                << R"(","session_id":")" << json_escape(task.session_id)
                << R"(","global_id":")" << json_escape(task.global_id)
                << R"(","workspace_id":")" << json_escape(workspace_id)
                << R"(","goal":")" << json_escape(input)
                << R"(","status":"running","user_message":{"id":")"
                << json_escape(userMessage.id) << R"(","session_id":")"
                << json_escape(userMessage.session_id) << R"(","task_id":")"
                << json_escape(task.id)
                << R"(","role":"user","message_type":"normal","content":")"
                << json_escape(userMessage.content) << R"(","sequence_no":)"
                << userMessage.sequence_no << R"(,"source_event_id":null,"created_at":")"
                << json_escape(userMessage.created_at) << R"("}}})";
  return http_response(response_body.str());
}

std::string extract_query_string(const std::string &request,
                                 const std::string &key) {
  const std::size_t request_line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, request_line_end);
  const std::string marker = key + "=";
  const std::size_t marker_pos = request_line.find(marker);
  if (marker_pos == std::string::npos) {
    return "";
  }
  const std::size_t value_start = marker_pos + marker.size();
  const std::size_t value_end = request_line.find_first_of("& ", value_start);
  const std::string raw =
      request_line.substr(value_start, value_end - value_start);
  // 简单 URL decode
  std::ostringstream decoded;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '%' && i + 2 < raw.size()) {
      const std::string hex = raw.substr(i + 1, 2);
      decoded << static_cast<char>(std::stoi(hex, nullptr, 16));
      i += 2;
    } else {
      decoded << raw[i];
    }
  }
  return decoded.str();
}

std::string TaskController::listTasks(const std::string &request) {
  std::vector<TaskRecord> tasks;
  try {
    if (DataAccessFacade::getInstance().isInitialized()) {
      const std::string session_id =
          extract_query_string(request, "session_id");
      if (!session_id.empty()) {
        tasks = DataAccessFacade::getInstance().listTasksBySession(session_id);
      } else {
        tasks = DataAccessFacade::getInstance().listRecentTasks(
            extract_query_int(request, "page_size", 20));
      }
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
         << json_escape(t.session_id) << R"(","global_id":")"
         << json_escape(t.global_id) << R"(","workspace_id":")"
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
         << R"(","global_id":")" << json_escape(task->global_id)
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
    auto &facade = DataAccessFacade::getInstance();
    const auto task = facade.getTask(task_id);
    if (!task) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
          "404 Not Found");
    }

    const bool cancelled =
        task->status == "interrupted"
            ? false
            : AgentOrchestrator::getInstance().cancelTask(task_id);
    const auto currentTask = facade.getTask(task_id);
    if (!currentTask) {
      return http_response(
          R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
          "404 Not Found");
    }

    LOG_INFO("TaskController::cancelTask: task={}, orchestrator_result={}, "
             "status={}",
             task_id, cancelled, currentTask->status);

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")"
         << json_escape(currentTask->id) << R"(","session_id":")"
         << json_escape(currentTask->session_id) << R"(","global_id":")"
         << json_escape(currentTask->global_id) << R"(","workspace_id":")"
         << json_escape(currentTask->workspace_id) << R"(","goal":")"
         << json_escape(currentTask->goal) << R"(","status":")"
         << json_escape(currentTask->status) << R"(")";
    if (!currentTask->plan.empty()) {
      body << R"(,"plan":)" << currentTask->plan;
    }
    if (!currentTask->current_step.empty()) {
      body << R"(,"current_step":")"
           << json_escape(currentTask->current_step)
           << R"(")";
    }
    body << R"(,"created_at":")" << json_escape(currentTask->created_at)
         << R"(","updated_at":")" << json_escape(currentTask->updated_at)
         << R"("}})";
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
           << json_escape(event.created_at) << R"(","sequence_no":)"
           << event.sequence_no << "}";
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

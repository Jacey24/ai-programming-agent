#include "DebugController.h"

#include "application/ToolSystem.h"
#include "domain/debug/BreakpointTypes.h"
#include "domain/debug/Debugger.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

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
  const std::size_t pos = request.find("\r\n\r\n");
  if (pos == std::string::npos)
    return "";
  return request.substr(pos + 4);
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

// Extract suffix portion of path after "/api/v1/debug/{taskId}/"
std::string extract_debug_suffix(const std::string &request,
                                 const std::string &taskId,
                                 const std::string &suffix) {
  const std::size_t line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, line_end);
  // Build the path: /api/v1/debug/{taskId}/{suffix}
  std::string fullPath = "/api/v1/debug/" + taskId + "/" + suffix;
  if (request_line.find(fullPath) != std::string::npos) {
    return taskId;
  }
  return "";
}

// Generic function to handle /api/v1/debug/{taskId}/xxx style requests
// Returns the taskId if the path suffix matches
std::string extract_task_id_with_suffix(const std::string &request,
                                        const std::string &suffix) {
  const std::size_t line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, line_end);
  const std::string prefix = "/api/v1/debug/";
  std::string marker = "/" + suffix;
  std::string marker_with_space = "/" + suffix + " ";

  const std::size_t prefix_pos = request_line.find(prefix);
  if (prefix_pos == std::string::npos)
    return "";

  // After prefix, extract until the marker
  std::string afterPrefix = request_line.substr(prefix_pos + prefix.size());
  const std::size_t markerPos = afterPrefix.find(marker_with_space);
  if (markerPos == std::string::npos) {
    return "";
  }
  return afterPrefix.substr(0, markerPos);
}

} // namespace

namespace codepilot {

// ── Debugger 不可用时的统一错误响应 ──
static std::string debugger_not_available() {
  return http_response(
      R"({"success":false,"error":{"code":"DEBUGGER_NOT_AVAILABLE","message":"Debugger is not enabled or not initialized"}})",
      "503 Service Unavailable");
}

static Debugger &getDebuggerChecked(std::string *errorOut) {
  if (!ToolSystem::getInstance().isInitialized()) {
    if (errorOut)
      *errorOut = debugger_not_available();
    static Debugger dummy(nullptr);
    return dummy;
  }
  auto &dbg = ToolSystem::getInstance().debugger();
  if (!dbg.isEnabled()) {
    if (errorOut)
      *errorOut = debugger_not_available();
    return dbg;
  }
  return dbg;
}

// ── 启停控制 ──
std::string DebugController::setEnabled(const std::string &request) const {
  if (!ToolSystem::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"SYSTEM_NOT_INITIALIZED","message":"ToolSystem is not initialized"}})",
        "503 Service Unavailable");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  if (!j.contains("enabled") || !j["enabled"].is_boolean()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"field 'enabled' (boolean) is required"}})",
        "400 Bad Request");
  }

  bool enabled = j["enabled"].get<bool>();
  ToolSystem::getInstance().setDebuggerEnabled(enabled);

  std::ostringstream body;
  body << R"({"success":true,"data":{"debugger_enabled":)"
       << (enabled ? "true" : "false") << "}}";
  return http_response(body.str());
}

// ── 断点管理 ──
std::string DebugController::setBreakpoint(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  if (!j.contains("tool") || !j["tool"].is_string() || !j.contains("type") ||
      !j["type"].is_string()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"fields 'tool' and 'type' are required"}})",
        "400 Bad Request");
  }

  std::string toolName = j["tool"].get<std::string>();
  BreakpointType type = breakpointTypeFromString(j["type"].get<std::string>());

  dbg.setBreakpoint(toolName, type);

  std::ostringstream body;
  body << R"({"success":true,"data":{"tool":")" << json_escape(toolName)
       << R"(","type":")" << json_escape(breakpointTypeToString(type))
       << R"("}})";
  return http_response(body.str());
}

std::string
DebugController::removeBreakpoint(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  if (!j.contains("tool") || !j["tool"].is_string() || !j.contains("type") ||
      !j["type"].is_string()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"fields 'tool' and 'type' are required"}})",
        "400 Bad Request");
  }

  std::string toolName = j["tool"].get<std::string>();
  BreakpointType type = breakpointTypeFromString(j["type"].get<std::string>());

  dbg.removeBreakpoint(toolName, type);

  std::ostringstream body;
  body << R"({"success":true,"data":{"removed":true,"tool":")"
       << json_escape(toolName) << R"(","type":")"
       << json_escape(breakpointTypeToString(type)) << R"("}})";
  return http_response(body.str());
}

std::string DebugController::listBreakpoints() const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  auto bps = dbg.listBreakpoints();

  std::ostringstream body;
  body << R"({"success":true,"data":{"breakpoints":[)";
  for (size_t i = 0; i < bps.size(); ++i) {
    if (i > 0)
      body << ",";
    body << R"({"tool":")" << json_escape(bps[i].toolName) << R"(",)"
         << R"("type":")" << json_escape(breakpointTypeToString(bps[i].type))
         << R"(","enabled":)" << (bps[i].enabled ? "true" : "false")
         << R"(,"hit_count":)" << bps[i].hitCount << "}";
  }
  body << "]}}";
  return http_response(body.str());
}

// ── 执行控制 ──
std::string DebugController::doContinue(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "continue");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/continue"}})",
        "400 Bad Request");
  }

  dbg.doContinue(taskId);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","action":"continue"}})";
  return http_response(body.str());
}

std::string DebugController::doStepOver(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "step_over");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/step_over"}})",
        "400 Bad Request");
  }

  dbg.doStepOver(taskId);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","action":"step_over"}})";
  return http_response(body.str());
}

std::string DebugController::doSkip(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "skip");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/skip"}})",
        "400 Bad Request");
  }

  // 从请求体中解析模拟结果
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  ToolResult mockResult = ToolResult::Ok("skipped");
  if (!j.is_discarded() && j.is_object() && j.contains("result")) {
    auto &res = j["result"];
    if (res.contains("success") && res["success"].is_boolean())
      mockResult.success = res["success"].get<bool>();
    if (res.contains("output") && res["output"].is_string())
      mockResult.output = res["output"].get<std::string>();
    if (res.contains("error") && res["error"].is_string())
      mockResult.error = res["error"].get<std::string>();
    if (res.contains("exit_code") && res["exit_code"].is_number_integer())
      mockResult.exitCode = res["exit_code"].get<int>();
  }

  dbg.doSkip(taskId, mockResult);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","action":"skip","mock_result":{"success":)"
       << (mockResult.success ? "true" : "false") << R"(,"output":")"
       << json_escape(mockResult.output) << R"("}})"
       << "}}";
  return http_response(body.str());
}

// ── 参数/结果修改 ──
std::string DebugController::modifyArguments(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "modify_args");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/modify_args"}})",
        "400 Bad Request");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("args")) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"field 'args' is required"}})",
        "400 Bad Request");
  }

  dbg.modifyArguments(taskId, j["args"]);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","action":"modify_args"}})";
  return http_response(body.str());
}

std::string DebugController::modifyResult(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "modify_result");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/modify_result"}})",
        "400 Bad Request");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  ToolResult newResult = ToolResult::Ok("modified");
  if (!j.is_discarded() && j.is_object() && j.contains("result")) {
    auto &res = j["result"];
    if (res.contains("success") && res["success"].is_boolean())
      newResult.success = res["success"].get<bool>();
    if (res.contains("output") && res["output"].is_string())
      newResult.output = res["output"].get<std::string>();
    if (res.contains("error") && res["error"].is_string())
      newResult.error = res["error"].get<std::string>();
    if (res.contains("exit_code") && res["exit_code"].is_number_integer())
      newResult.exitCode = res["exit_code"].get<int>();
  }

  dbg.modifyResult(taskId, newResult);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","action":"modify_result"}})";
  return http_response(body.str());
}

// ── 状态查询 ──
std::string DebugController::getState(const std::string &request) const {
  std::string errMsg;
  auto &dbg = getDebuggerChecked(&errMsg);
  if (!errMsg.empty())
    return errMsg;

  std::string taskId = extract_task_id_with_suffix(request, "state");
  if (taskId.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must be /api/v1/debug/{taskId}/state"}})",
        "400 Bad Request");
  }

  DebugState state = dbg.getState(taskId);
  PausedContext ctx = dbg.getPausedContext(taskId);

  std::ostringstream body;
  body << R"({"success":true,"data":{"task_id":")" << json_escape(taskId)
       << R"(","state":")" << json_escape(debugStateToString(state)) << R"(")"
       << R"(,"paused_context":)" << ctx.serialize().dump() << "}}";
  return http_response(body.str());
}

} // namespace codepilot
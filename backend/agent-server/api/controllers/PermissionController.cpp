#include "PermissionController.h"

#include "application/ToolSystem.h"
#include "domain/security/PermissionManager.h"
#include "domain/tools/Tool.h"

#include <exception>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace {

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
  return request_line.substr(value_start, value_end - value_start);
}

} // namespace

namespace codepilot {

// 将内存中的权限请求序列化为 API JSON（字段与前端 main.js 对齐）。
static std::string permission_to_json(const PermissionRequest &perm) {
  const std::string args =
      perm.arguments.is_null() ? std::string("") : perm.arguments.dump();
  std::ostringstream body;
  body << R"({"id":")" << json_escape(perm.id) << R"(","task_id":")"
       << json_escape(perm.taskId) << R"(","tool_name":")"
       << json_escape(perm.toolName) << R"(","risk_level":")"
       << json_escape(riskLevelToString(perm.riskLevel)) << R"(","action":")"
       << json_escape(perm.toolName) << R"(","reason":")" << json_escape(args)
       << R"(","status":")" << json_escape(perm.statusToString())
       << R"(","created_at":")" << json_escape(perm.createdAt) << R"(")";
  if (!perm.resolvedAt.empty()) {
    body << R"(,"resolved_at":")" << json_escape(perm.resolvedAt) << R"(")";
  }
  body << "}";
  return body.str();
}

PermissionController::PermissionController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string PermissionController::listPermissions(const std::string &request) {
  const std::string task_id = extract_query_string(request, "task_id");

  if (!ToolSystem::getInstance().isInitialized()) {
    return http_response(R"({"success":true,"data":{"items":[]}})");
  }

  const auto permissions =
      ToolSystem::getInstance().permissionManager().getPendingRequests(task_id);

  std::ostringstream body;
  body << R"({"success":true,"data":{"items":[)";
  for (std::size_t i = 0; i < permissions.size(); ++i) {
    if (i > 0) {
      body << ",";
    }
    body << permission_to_json(permissions[i]);
  }
  body << "]}}";
  return http_response(body.str());
}

std::string PermissionController::getPermission(const std::string &request) {
  const std::string perm_id =
      extract_path_segment(request, "/api/v1/permissions/");
  if (perm_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"permission id is required"}})",
        "400 Bad Request");
  }

  if (!ToolSystem::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
        "404 Not Found");
  }

  const auto perm =
      ToolSystem::getInstance().permissionManager().getRequestCopy(perm_id);
  if (!perm) {
    return http_response(
        R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
        "404 Not Found");
  }

  std::ostringstream body;
  body << R"({"success":true,"data":)" << permission_to_json(*perm) << "}";
  return http_response(body.str());
}

std::string PermissionController::handleAction(const std::string &request) {
  const std::string full_segment =
      extract_path_segment(request, "/api/v1/permissions/");
  const std::string approve_suffix = "/approve";
  const std::string reject_suffix = "/reject";

  const bool is_approve =
      full_segment.size() > approve_suffix.size() &&
      full_segment.compare(full_segment.size() - approve_suffix.size(),
                           approve_suffix.size(), approve_suffix) == 0;
  const bool is_reject =
      full_segment.size() > reject_suffix.size() &&
      full_segment.compare(full_segment.size() - reject_suffix.size(),
                           reject_suffix.size(), reject_suffix) == 0;

  if (!is_approve && !is_reject) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /approve or /reject"}})",
        "400 Bad Request");
  }

  const std::string suffix = is_approve ? approve_suffix : reject_suffix;
  const std::string perm_id =
      full_segment.substr(0, full_segment.size() - suffix.size());
  if (perm_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"permission id is required"}})",
        "400 Bad Request");
  }

  if (!ToolSystem::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
        "404 Not Found");
  }

  auto &manager = ToolSystem::getInstance().permissionManager();
  const auto existing = manager.getRequestCopy(perm_id);
  if (!existing) {
    return http_response(
        R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
        "404 Not Found");
  }
  if (existing->statusToString() != "pending") {
    return http_response(
        R"({"success":false,"error":{"code":"ALREADY_RESOLVED","message":"permission request already resolved"}})",
        "400 Bad Request");
  }

  // ★ 关键修复：直接解析内存中的权限请求，唤醒正在 waitForResolution 阻塞的
  // Agent 线程。
  const bool ok = manager.resolvePermission(perm_id, is_approve);
  if (!ok) {
    return http_response(
        R"({"success":false,"error":{"code":"INTERNAL_ERROR","message":"failed to resolve permission"}})",
        "500 Internal Server Error");
  }

  const std::string new_status = is_approve ? "approved" : "rejected";
  std::ostringstream body;
  body << R"({"success":true,"data":{"id":")" << json_escape(perm_id)
       << R"(","status":")" << new_status << R"("}})";
  return http_response(body.str());
}

std::string
PermissionController::approveFirstPending(const std::string & /*request*/) {
  if (!ToolSystem::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"NO_PENDING","message":"no pending permission requests"}})",
        "404 Not Found");
  }

  auto &manager = ToolSystem::getInstance().permissionManager();
  const auto pending = manager.getPendingRequests("");
  if (pending.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"NO_PENDING","message":"no pending permission requests"}})",
        "404 Not Found");
  }

  const std::string perm_id = pending[0].id;
  const bool ok = manager.resolvePermission(perm_id, true);
  if (!ok) {
    return http_response(
        R"({"success":false,"error":{"code":"INTERNAL_ERROR","message":"failed to resolve permission"}})",
        "500 Internal Server Error");
  }

  std::ostringstream body;
  body << R"({"success":true,"data":{"id":")" << json_escape(perm_id)
       << R"(","status":"approved"}})";
  return http_response(body.str());
}

} // namespace codepilot

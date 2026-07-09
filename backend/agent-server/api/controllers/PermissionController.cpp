#include "PermissionController.h"

#include "application/PermissionService.h"

#include <exception>
#include <sstream>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n";  break;
            case '\r': escaped << "\\r";  break;
            case '\t': escaped << "\\t";  break;
            default:   escaped << ch;
        }
    }
    return escaped.str();
}

std::string http_response(const std::string& body, const std::string& status = "200 OK") {
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

std::string extract_path_segment(const std::string& request, const std::string& prefix) {
    const std::size_t request_line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, request_line_end);
    const std::size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos) return "";
    const std::size_t path_start = method_end + 1;
    const std::size_t prefix_pos = request_line.find(prefix, path_start);
    if (prefix_pos == std::string::npos) return "";
    const std::size_t segment_start = prefix_pos + prefix.size();
    const std::size_t segment_end = request_line.find_first_of("? ", segment_start);
    if (segment_end == std::string::npos) {
        return request_line.substr(segment_start);
    }
    return request_line.substr(segment_start, segment_end - segment_start);
}

std::string extract_query_string(const std::string& request, const std::string& key) {
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

PermissionController::PermissionController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string PermissionController::listPermissions(const std::string& request) {
    const std::string task_id = extract_query_string(request, "task_id");

    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::vector<PermissionRequest> permissions;
    try {
        PermissionService service(db);
        permissions = service.listPendingRequests(task_id);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < permissions.size(); ++i) {
        const auto& perm = permissions[i];
        if (i > 0) {
            body << ",";
        }
        body << R"({"id":")" << json_escape(perm.id)
             << R"(","task_id":")" << json_escape(perm.task_id)
             << R"(","tool_name":")" << json_escape(perm.tool_name)
             << R"(","risk_level":")" << json_escape(perm.risk_level)
             << R"(","action":")" << json_escape(perm.action)
             << R"(","reason":")" << json_escape(perm.reason)
             << R"(","status":")" << json_escape(perm.status)
             << R"(","created_at":")" << json_escape(perm.created_at) << R"(")";
        if (!perm.resolved_at.empty()) {
            body << R"(,"resolved_at":")" << json_escape(perm.resolved_at) << R"(")";
        }
        body << "}";
    }
    body << "]}}";
    return http_response(body.str());
}

std::string PermissionController::getPermission(const std::string& request) {
    const std::string perm_id = extract_path_segment(request, "/api/v1/permissions/");
    if (perm_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"permission id is required"}})",
            "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::string response;
    try {
        PermissionService service(db);
        const auto perm = service.getRequest(perm_id);
        if (!perm) {
            response = http_response(
                R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
                "404 Not Found");
            sqlite3_close(db);
            return response;
        }

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(perm->id)
             << R"(","task_id":")" << json_escape(perm->task_id)
             << R"(","tool_name":")" << json_escape(perm->tool_name)
             << R"(","risk_level":")" << json_escape(perm->risk_level)
             << R"(","action":")" << json_escape(perm->action)
             << R"(","reason":")" << json_escape(perm->reason)
             << R"(","status":")" << json_escape(perm->status)
             << R"(","created_at":")" << json_escape(perm->created_at) << R"(")";
        if (!perm->resolved_at.empty()) {
            body << R"(,"resolved_at":")" << json_escape(perm->resolved_at) << R"(")";
        }
        body << "}}";
        response = http_response(body.str());
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);
    return response;
}

std::string PermissionController::handleAction(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/permissions/");
    const std::string approve_suffix = "/approve";
    const std::string reject_suffix = "/reject";

    const bool is_approve = full_segment.size() > approve_suffix.size() &&
        full_segment.compare(full_segment.size() - approve_suffix.size(), approve_suffix.size(), approve_suffix) == 0;
    const bool is_reject = full_segment.size() > reject_suffix.size() &&
        full_segment.compare(full_segment.size() - reject_suffix.size(), reject_suffix.size(), reject_suffix) == 0;

    if (!is_approve && !is_reject) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /approve or /reject"}})",
            "400 Bad Request");
    }

    const std::string suffix = is_approve ? approve_suffix : reject_suffix;
    const std::string perm_id = full_segment.substr(0, full_segment.size() - suffix.size());
    if (perm_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"permission id is required"}})",
            "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::string response;
    try {
        PermissionService service(db);

        const auto perm = service.getRequest(perm_id);
        if (!perm) {
            response = http_response(
                R"({"success":false,"error":{"code":"PERMISSION_NOT_FOUND","message":"permission request not found"}})",
                "404 Not Found");
            sqlite3_close(db);
            return response;
        }

        if (perm->status != "pending") {
            response = http_response(
                R"({"success":false,"error":{"code":"ALREADY_RESOLVED","message":"permission request already resolved"}})",
                "400 Bad Request");
            sqlite3_close(db);
            return response;
        }

        const bool ok = is_approve ? service.approveRequest(perm_id) : service.rejectRequest(perm_id);
        if (!ok) {
            response = http_response(
                R"({"success":false,"error":{"code":"INTERNAL_ERROR","message":"failed to update permission status"}})",
                "500 Internal Server Error");
            sqlite3_close(db);
            return response;
        }

        const auto updated = service.getRequest(perm_id);
        const std::string new_status = is_approve ? "approved" : "rejected";
        const std::string resolved_at = updated ? updated->resolved_at : "";

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(perm_id)
             << R"(","status":")" << new_status << R"(")";
        if (!resolved_at.empty()) {
            body << R"(,"resolved_at":")" << json_escape(resolved_at) << R"(")";
        }
        body << "}}";
        response = http_response(body.str());
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);
    return response;
}

} // namespace codepilot

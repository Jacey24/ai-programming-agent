#include "ReplayController.h"

#include "application/ReplayService.h"

#include <exception>
#include <sstream>
#include <utility>

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

} // namespace

namespace codepilot {

ReplayController::ReplayController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string ReplayController::getReplay(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/tasks/");
    const std::string suffix = "/replay";
    if (full_segment.size() <= suffix.size() ||
        full_segment.compare(full_segment.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /replay"}})",
            "400 Bad Request");
    }
    const std::string task_id = full_segment.substr(0, full_segment.size() - suffix.size());
    if (task_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
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
        ReplayService service(db);
        const ReplayResult result = service.buildReplay(task_id);

        std::ostringstream body;
        body << R"({"success":true,"data":{"task":{)";
        body << R"("id":")" << json_escape(result.task.id)
             << R"(","goal":")" << json_escape(result.task.goal)
             << R"(","status":")" << json_escape(result.task.status)
             << R"("})";
        body << R"(,"timeline":[)";
        for (std::size_t i = 0; i < result.timeline.size(); ++i) {
            const auto& item = result.timeline[i];
            if (i > 0) {
                body << ",";
            }
            body << R"({"type":")" << json_escape(item.type)
                 << R"(","content":")" << json_escape(item.content)
                 << R"(","created_at":")" << json_escape(item.created_at) << R"(")";
            if (!item.tool_name.empty()) {
                body << R"(,"tool_name":")" << json_escape(item.tool_name) << R"(")";
            }
            if (!item.file_path.empty()) {
                body << R"(,"file_path":")" << json_escape(item.file_path) << R"(")";
            }
            body << "}";
        }
        body << "]}}";
        response = http_response(body.str());
    } catch (const std::exception& error) {
        sqlite3_close(db);
        const std::string msg = error.what();
        if (msg.find("not found") != std::string::npos) {
            return http_response(
                R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":")" + json_escape(msg) + R"("}})",
                "404 Not Found");
        }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(msg) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);
    return response;
}

} // namespace codepilot

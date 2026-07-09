#include "FileChangeController.h"

#include "application/FileChangeService.h"

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

} // namespace

namespace codepilot {

FileChangeController::FileChangeController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string FileChangeController::listFileChanges(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/tasks/");
    const std::string suffix = "/file-changes";
    if (full_segment.size() <= suffix.size() ||
        full_segment.compare(full_segment.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /file-changes"}})",
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

    std::vector<FileChangeRecord> file_changes;
    try {
        FileChangeService service(db);
        file_changes = service.getFileChangesByTaskId(task_id);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < file_changes.size(); ++i) {
        const auto& fc = file_changes[i];
        if (i > 0) {
            body << ",";
        }
        body << R"({"id":")" << json_escape(fc.id)
             << R"(","task_id":")" << json_escape(fc.task_id)
             << R"(","file_path":")" << json_escape(fc.file_path)
             << R"(","change_type":")" << json_escape(fc.change_type)
             << R"(","created_at":")" << json_escape(fc.created_at)
             << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
}

std::string FileChangeController::getFileChange(const std::string& request) {
    const std::string file_change_id = extract_path_segment(request, "/api/v1/file-changes/");
    if (file_change_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"file_change_id is required"}})",
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
        FileChangeService service(db);
        const auto fc = service.getFileChangeById(file_change_id);
        if (!fc) {
            response = http_response(
                R"({"success":false,"error":{"code":"FILE_CHANGE_NOT_FOUND","message":"file change not found"}})",
                "404 Not Found");
            sqlite3_close(db);
            return response;
        }

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(fc->id)
             << R"(","task_id":")" << json_escape(fc->task_id)
             << R"(","file_path":")" << json_escape(fc->file_path)
             << R"(","change_type":")" << json_escape(fc->change_type)
             << R"(","diff":")" << json_escape(fc->diff)
             << R"(","created_at":")" << json_escape(fc->created_at)
             << R"("}})";
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

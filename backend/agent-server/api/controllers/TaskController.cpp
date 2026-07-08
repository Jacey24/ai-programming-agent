#include "TaskController.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <sqlite3.h>

namespace {

// ---- database path (mirrors AppBootstrap.cpp default) ----
constexpr const char* kDatabasePath = "/data/agent.db";

// ---- utility helpers (self-contained copies from AppBootstrap.cpp) ----

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

std::string current_timestamp() {
    const auto now = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

std::string generate_id(const std::string& prefix) {
    return prefix + "_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t marker_pos = body.find(marker);
    if (marker_pos == std::string::npos) return "";

    const std::size_t colon_pos = body.find(':', marker_pos + marker.size());
    const std::size_t first_quote = body.find('"', colon_pos);
    if (colon_pos == std::string::npos || first_quote == std::string::npos) return "";

    std::string value;
    bool escaped = false;
    for (std::size_t i = first_quote + 1; i < body.size(); ++i) {
        const char ch = body[i];
        if (escaped) {
            switch (ch) {
                case 'n':  value.push_back('\n'); break;
                case 'r':  value.push_back('\r'); break;
                case 't':  value.push_back('\t'); break;
                default:   value.push_back(ch);
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') { escaped = true; continue; }
        if (ch == '"')  break;
        value.push_back(ch);
    }
    return value;
}

std::string request_body(const std::string& request) {
    const std::size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) return "";
    return request.substr(body_pos + 4);
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
    const std::size_t segment_end = request_line.find(' ', segment_start);
    if (segment_end == std::string::npos)
        return request_line.substr(segment_start);
    return request_line.substr(segment_start, segment_end - segment_start);
}

} // anonymous namespace

namespace codepilot {

std::string TaskController::createTask(const std::string& request) {
    const std::string body_content = request_body(request);
    const std::string session_id = extract_json_string(body_content, "session_id");
    const std::string workspace_id = extract_json_string(body_content, "workspace_id");
    const std::string input = extract_json_string(body_content, "input");
    if (session_id.empty() || workspace_id.empty() || input.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id, workspace_id and input are required"}})",
            "400 Bad Request");
    }

    const std::string id = generate_id("task");
    const std::string now = current_timestamp();

    sqlite3* db = nullptr;
    if (sqlite3_open(kDatabasePath, &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    const char* sql = "INSERT INTO tasks (id, session_id, workspace_id, goal, status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, input.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, "created", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step_result != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    // TODO Sprint 2: AgentService::runTask(id, session_id, workspace_id, input)

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")" << json_escape(id)
                  << R"(","session_id":")" << json_escape(session_id)
                  << R"(","workspace_id":")" << json_escape(workspace_id)
                  << R"(","goal":")" << json_escape(input)
                  << R"(","status":"created")"
                  << R"(,"created_at":")" << now
                  << R"(","updated_at":")" << now << R"("}})";
    return http_response(response_body.str());
}

std::string TaskController::getTask(const std::string& request) {
    const std::string task_id = extract_path_segment(request, "/api/v1/tasks/");
    if (task_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
            "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kDatabasePath, &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    const char* sql = "SELECT id, session_id, workspace_id, goal, status, plan, current_step, created_at, updated_at FROM tasks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::string response;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* workspace_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* goal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* plan = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* current_step = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        const auto* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(id ? id : "")
             << R"(","session_id":")" << json_escape(session_id ? session_id : "")
             << R"(","workspace_id":")" << json_escape(workspace_id ? workspace_id : "")
             << R"(","goal":")" << json_escape(goal ? goal : "")
             << R"(","status":")" << json_escape(status ? status : "") << R"(")";
        if (plan && plan[0] != '\0') {
            body << R"(","plan":)" << plan;
        }
        if (current_step && current_step[0] != '\0') {
            body << R"(,"current_step":")" << json_escape(current_step) << R"(")";
        }
        body << R"(,"created_at":")" << json_escape(created_at ? created_at : "")
             << R"(","updated_at":")" << json_escape(updated_at ? updated_at : "")
             << R"("}})";
        response = http_response(body.str());
    } else {
        response = http_response(
            R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"任务不存在"}})",
            "404 Not Found");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return response;
}

} // namespace codepilot

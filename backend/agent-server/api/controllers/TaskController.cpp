#include "TaskController.h"

#include "application/AgentService.h"
#include "application/TaskService.h"

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
    const std::size_t segment_end = request_line.find_first_of("? ", segment_start);
    if (segment_end == std::string::npos) {
        return request_line.substr(segment_start);
    }
    return request_line.substr(segment_start, segment_end - segment_start);
}

int extract_query_int(const std::string& request, const std::string& key, int fallback) {
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

    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    TaskRecord task;
    try {
        TaskService task_service(db);
        task = task_service.createTask(session_id, workspace_id, input);

        AgentService agent_service;
        const AgentResult agent_result = agent_service.runTask(task.id, session_id, workspace_id, input);
        task_service.updateExecution(
            task.id,
            agent_result.status.empty() ? "planned" : agent_result.status,
            agent_result.planJson,
            agent_result.currentStep);

        const auto updated = task_service.getTask(task.id);
        if (updated) {
            task = *updated;
        }
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

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
        response_body << R"(,"current_step":")" << json_escape(task.current_step) << R"(")";
    }
    response_body << R"(,"created_at":")" << json_escape(task.created_at)
                  << R"(","updated_at":")" << json_escape(task.updated_at) << R"("}})";
    return http_response(response_body.str());
}

std::string TaskController::listTasks(const std::string& request) {
    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::vector<TaskRecord> tasks;
    try {
        TaskService service(db);
        tasks = service.listTasks(extract_query_int(request, "page_size", 20));
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        if (i > 0) {
            body << ",";
        }
        body << R"({"id":")" << json_escape(task.id)
             << R"(","session_id":")" << json_escape(task.session_id)
             << R"(","workspace_id":")" << json_escape(task.workspace_id)
             << R"(","goal":")" << json_escape(task.goal)
             << R"(","status":")" << json_escape(task.status)
             << R"(","created_at":")" << json_escape(task.created_at)
             << R"(","updated_at":")" << json_escape(task.updated_at)
             << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
}

std::string TaskController::getTask(const std::string& request) {
    const std::string task_id = extract_path_segment(request, "/api/v1/tasks/");
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
        TaskService service(db);
        const auto task = service.getTask(task_id);
        if (!task) {
            response = http_response(
                R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"task not found"}})",
                "404 Not Found");
            sqlite3_close(db);
            return response;
        }

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(task->id)
             << R"(","session_id":")" << json_escape(task->session_id)
             << R"(","workspace_id":")" << json_escape(task->workspace_id)
             << R"(","goal":")" << json_escape(task->goal)
             << R"(","status":")" << json_escape(task->status) << R"(")";
        if (!task->plan.empty()) {
            body << R"(,"plan":)" << task->plan;
        }
        if (!task->current_step.empty()) {
            body << R"(,"current_step":")" << json_escape(task->current_step) << R"(")";
        }
        body << R"(,"created_at":")" << json_escape(task->created_at)
             << R"(","updated_at":")" << json_escape(task->updated_at)
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

std::string TaskController::cancelTask(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/tasks/");
    const std::string cancel_suffix = "/cancel";
    if (full_segment.size() <= cancel_suffix.size() ||
        full_segment.compare(full_segment.size() - cancel_suffix.size(), cancel_suffix.size(), cancel_suffix) != 0) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /cancel"}})",
            "400 Bad Request");
    }
    const std::string task_id = full_segment.substr(0, full_segment.size() - cancel_suffix.size());
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
        TaskService service(db);
        const TaskRecord task = service.cancelTask(task_id);

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
             << R"(","session_id":")" << json_escape(task.session_id)
             << R"(","workspace_id":")" << json_escape(task.workspace_id)
             << R"(","goal":")" << json_escape(task.goal)
             << R"(","status":")" << json_escape(task.status) << R"(")";
        if (!task.plan.empty()) {
            body << R"(,"plan":)" << task.plan;
        }
        if (!task.current_step.empty()) {
            body << R"(,"current_step":")" << json_escape(task.current_step) << R"(")";
        }
        body << R"(,"created_at":")" << json_escape(task.created_at)
             << R"(","updated_at":")" << json_escape(task.updated_at) << R"("}})";
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

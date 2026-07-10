#include "TaskController.h"

#include "application/AgentService.h"
#include "application/ToolSystem.h"
#include "application/LogService.h"
#include "application/TaskService.h"
#include "infrastructure/storage/repositories/EventRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include <exception>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace {

using json = nlohmann::json;

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

std::string json_string_value(const json& body, const std::string& key) {
    if (!body.contains(key) || !body.at(key).is_string()) {
        return "";
    }
    return body.at(key).get<std::string>();
}

std::string log_type_from_content(const std::string& content) {
    if (content.size() > 2 && content.front() == '[') {
        const auto end = content.find(']');
        if (end != std::string::npos && end > 1) {
            return content.substr(1, end - 1);
        }
    }
    return "agent";
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
    json body = json::parse(body_content, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
            "400 Bad Request");
    }

    const std::string session_id = json_string_value(body, "session_id");
    const std::string workspace_id = json_string_value(body, "workspace_id");
    const std::string input = json_string_value(body, "input");
    if (session_id.empty() || workspace_id.empty() || input.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id, workspace_id and input are required"}})",
            "400 Bad Request");
    }
    const json options = body.contains("options") && body["options"].is_object()
        ? body["options"] : json::object();
    TaskRunOptions runOptions;
    const std::string executionMode = options.value("execution_mode", "auto");
    runOptions.mode = executionMode == "answer" ? ExecutionMode::DirectAnswer
        : executionMode == "workspace" ? ExecutionMode::WorkspaceAgent : ExecutionMode::Auto;
    runOptions.autoRunSafeCommands = options.value("auto_run_safe_commands", true);
    runOptions.requireFileWritePermission = options.value("require_permission_for_file_write", true);
    runOptions.maxSteps = std::clamp(options.value("max_steps", 6), 1, 20);
    runOptions.maxRoundsPerStep = std::clamp(options.value("max_rounds_per_step", 3), 1, 6);

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
        // 先置为 running，真正的执行在后台线程里异步完成
        task_service.updateExecution(task.id, "running", task.plan, task.current_step);
        const auto refreshed = task_service.getTask(task.id);
        if (refreshed) {
            task = *refreshed;
        }
        EventData created = EventData::Create(task.id, EventType::TaskCreated,
            "任务已创建，正在启动 Agent", json{{"status", "running"}});
        EventRepository(db).create(created.id, created.taskId, created.typeToString(), created.content, created.metadata.dump());
        if (ToolSystem::getInstance().isInitialized()) {
            ToolSystem::getInstance().eventBus().publish(created);
        }
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    // 后台异步执行 Agent：自开一个独立的 SQLite 连接，
    // 执行过程中产生的事件通过 EventBus 实时推送给 SSE 连接。
    const std::string db_path = databasePath_;
    const std::string task_id = task.id;
    std::thread([db_path, task_id, session_id, workspace_id, input, runOptions]() {
        sqlite3* worker_db = nullptr;
        if (sqlite3_open(db_path.c_str(), &worker_db) != SQLITE_OK) {
            if (worker_db) { sqlite3_close(worker_db); }
            return;
        }
        try {
            AgentService agent_service;
            const AgentResult agent_result =
                agent_service.runTask(task_id, session_id, workspace_id, input, worker_db, runOptions);
            TaskService task_service(worker_db);
            task_service.updateExecution(
                task_id,
                agent_result.status.empty() ? "completed" : agent_result.status,
                agent_result.planJson,
                agent_result.currentStep);
            LogService log_service(worker_db);
            for (const auto& entry : agent_result.logs) {
                log_service.createLog(task_id, log_type_from_content(entry), entry);
            }
        } catch (const std::exception&) {
            try {
                TaskService task_service(worker_db);
                task_service.updateExecution(task_id, "failed", "", "");
            } catch (...) {
            }
        }
        sqlite3_close(worker_db);
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

std::string TaskController::listToolCalls(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/tasks/");
    const std::string suffix = "/tool-calls";
    if (full_segment.size() <= suffix.size() ||
        full_segment.compare(full_segment.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /tool-calls"}})",
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
        ToolCallRepository repo(db);
        const std::vector<ToolCallRecord> calls = repo.findByTaskId(task_id);

        std::ostringstream body;
        body << R"({"success":true,"data":{"task_id":")" << json_escape(task_id)
             << R"(","items":[)";
        for (std::size_t i = 0; i < calls.size(); ++i) {
            const auto& call = calls[i];
            if (i > 0) {
                body << ",";
            }
            body << R"({"id":")" << json_escape(call.id)
                 << R"(","task_id":")" << json_escape(call.task_id)
                 << R"(","tool_name":")" << json_escape(call.tool_name)
                 << R"(","arguments":")" << json_escape(call.arguments)
                 << R"(","success":)" << (call.success ? "true" : "false")
                 << R"(,"result":")" << json_escape(call.result)
                 << R"(","exit_code":)" << call.exit_code
                 << R"(,"created_at":")" << json_escape(call.created_at) << R"("})";
        }
        body << "]}}";
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

std::string TaskController::listEventHistory(const std::string& request) {
    const std::string full_segment = extract_path_segment(request, "/api/v1/tasks/");
    const std::string suffix = "/events/history";
    if (full_segment.size() <= suffix.size() ||
        full_segment.compare(full_segment.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /events/history"}})",
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
        EventRepository repo(db);
        const std::vector<EventRecord> events = repo.findByTaskId(task_id);

        std::ostringstream body;
        body << R"({"success":true,"data":{"task_id":")" << json_escape(task_id)
             << R"(","items":[)";
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            if (i > 0) {
                body << ",";
            }
            body << R"({"id":")" << json_escape(event.id)
                 << R"(","task_id":")" << json_escape(event.task_id)
                 << R"(","type":")" << json_escape(event.type)
                 << R"(","content":")" << json_escape(event.content)
                 << R"(","metadata":)";
            json metadata = json::parse(event.metadata, nullptr, false);
            if (metadata.is_discarded()) {
                metadata = json::object();
            }
            body << metadata.dump()
                 << R"(,"created_at":")" << json_escape(event.created_at) << R"("})";
        }
        body << "]}}";
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

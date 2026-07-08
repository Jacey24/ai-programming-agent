#include <chrono>
#include <csignal>
#include <ctime>
#include <exception>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sqlite3.h>

#include "api/controllers/SessionController.h"
#include "api/controllers/TaskController.h"
#include "api/controllers/WorkspaceController.h"
#include "infrastructure/storage/repositories/LogRepository.h"
#include "infrastructure/storage/repositories/SessionRepository.h"
#include "infrastructure/storage/repositories/TaskRepository.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t running = 1;

struct DatabaseState {
    std::string path{"/data/agent.db"};
    bool connected{false};
    std::string error;
};

DatabaseState database_state;

void handle_signal(int) {
    running = 0;
}

std::string json_escape(const std::string& value) {
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

std::string extract_storage_path(const std::string& config_path) {
    FILE* file = std::fopen(config_path.c_str(), "rb");
    if (!file) {
        return database_state.path;
    }

    std::string content;
    char buffer[4096] = {};
    while (const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file)) {
        content.append(buffer, read);
    }
    std::fclose(file);

    const std::string marker = R"("path")";
    const std::size_t marker_pos = content.find(marker);
    if (marker_pos == std::string::npos) {
        return database_state.path;
    }

    const std::size_t colon_pos = content.find(':', marker_pos + marker.size());
    const std::size_t first_quote = content.find('"', colon_pos);
    const std::size_t second_quote = content.find('"', first_quote + 1);
    if (colon_pos == std::string::npos || first_quote == std::string::npos || second_quote == std::string::npos) {
        return database_state.path;
    }

    return content.substr(first_quote + 1, second_quote - first_quote - 1);
}

bool initialize_database(const std::string& path) {
    database_state.path = path;
    sqlite3* db = nullptr;
    const int open_result = sqlite3_open(path.c_str(), &db);
    if (open_result != SQLITE_OK) {
        database_state.connected = false;
        database_state.error = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }

    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS system_health (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    service TEXT NOT NULL,
    checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO system_health (id, service, checked_at)
VALUES (1, 'codepilot-agent-server', CURRENT_TIMESTAMP)
ON CONFLICT(id) DO UPDATE SET checked_at = CURRENT_TIMESTAMP;

CREATE TABLE IF NOT EXISTS chat_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    prompt TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS workspaces (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    created_at TEXT NOT NULL
);
)SQL";

    char* error_message = nullptr;
    const int exec_result = sqlite3_exec(db, schema, nullptr, nullptr, &error_message);
    if (exec_result != SQLITE_OK) {
        database_state.connected = false;
        database_state.error = error_message ? error_message : "schema initialization failed";
        sqlite3_free(error_message);
        sqlite3_close(db);
        return false;
    }

    try {
        SessionRepository session_repository(db);
        session_repository.initTable();

        TaskRepository task_repository(db);
        task_repository.initTable();

        LogRepository log_repository(db);
        log_repository.initTable();
    } catch (const std::exception& error) {
        database_state.connected = false;
        database_state.error = error.what();
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const int prepare_result = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM system_health;", -1, &stmt, nullptr);
    if (prepare_result != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        database_state.connected = false;
        database_state.error = sqlite3_errmsg(db);
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    database_state.connected = true;
    database_state.error.clear();
    return true;
}

std::string health_response() {
    std::ostringstream body_stream;
    body_stream << R"({"success":true,"data":{"status":"ok","service":"codepilot-agent-server","version":"0.1.0","database":{"type":"sqlite","connected":)"
                << (database_state.connected ? "true" : "false")
                << R"(,"path":")" << json_escape(database_state.path) << R"(")";
    if (!database_state.error.empty()) {
        body_stream << R"(,"error":")" << json_escape(database_state.error) << R"(")";
    }
    body_stream << "}}}";
    const std::string body = body_stream.str();
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;
    return response.str();
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

std::string options_response() {
    return "HTTP/1.1 204 No Content\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type\r\n"
           "Connection: close\r\n"
           "Content-Length: 0\r\n\r\n";
}

std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t marker_pos = body.find(marker);
    if (marker_pos == std::string::npos) {
        return "";
    }

    const std::size_t colon_pos = body.find(':', marker_pos + marker.size());
    const std::size_t first_quote = body.find('"', colon_pos);
    if (colon_pos == std::string::npos || first_quote == std::string::npos) {
        return "";
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = first_quote + 1; i < body.size(); ++i) {
        const char ch = body[i];
        if (escaped) {
            switch (ch) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(ch);
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        value.push_back(ch);
    }
    return value;
}

std::string request_body(const std::string& request) {
    const std::size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        return "";
    }
    return request.substr(body_pos + 4);
}

std::string query_parameter(const std::string& request, const std::string& key) {
    const std::size_t request_line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, request_line_end);
    const std::size_t query_start = request_line.find('?');
    if (query_start == std::string::npos) {
        return "";
    }

    const std::size_t path_end = request_line.find(' ', query_start);
    const std::string query = request_line.substr(query_start + 1, path_end - query_start - 1);
    const std::string marker = key + "=";
    std::size_t param_start = 0;
    while (param_start < query.size()) {
        const std::size_t param_end = query.find('&', param_start);
        const std::string param = query.substr(param_start, param_end - param_start);
        if (param.rfind(marker, 0) == 0) {
            return param.substr(marker.size());
        }
        if (param_end == std::string::npos) {
            break;
        }
        param_start = param_end + 1;
    }
    return "";
}

std::size_t content_length(const std::string& request) {
    const std::string header = "Content-Length:";
    const std::size_t header_pos = request.find(header);
    if (header_pos == std::string::npos) {
        return 0;
    }

    const std::size_t value_start = header_pos + header.size();
    const std::size_t line_end = request.find("\r\n", value_start);
    const std::string value = request.substr(value_start, line_end - value_start);
    try {
        return static_cast<std::size_t>(std::stoul(value));
    } catch (...) {
        return 0;
    }
}

std::string read_http_request(int client_fd) {
    std::string request;
    char buffer[4096] = {};

    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return request;
        }
        request.append(buffer, static_cast<std::size_t>(received));
        if (request.size() > 1024 * 1024) {
            return request;
        }
    }

    const std::size_t body_start = request.find("\r\n\r\n") + 4;
    const std::size_t expected_body_size = content_length(request);
    while (request.size() - body_start < expected_body_size) {
        const ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(received));
        if (request.size() > 1024 * 1024) {
            break;
        }
    }

    return request;
}

std::string create_chat_response(const std::string& request) {
    const std::string prompt = extract_json_string(request_body(request), "prompt");
    if (prompt.empty()) {
        return http_response(R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"prompt is required"}})", "400 Bad Request");
    }

    constexpr const char* ai_response = "待开发";
    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) {
            sqlite3_close(db);
        }
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO chat_messages (prompt, response) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    sqlite3_bind_text(stmt, 1, prompt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ai_response, -1, SQLITE_TRANSIENT);
    const int step_result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step_result != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    const sqlite3_int64 id = sqlite3_last_insert_rowid(db);
    sqlite3_close(db);

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":)" << id
         << R"(,"prompt":")" << json_escape(prompt)
         << R"(","response":")" << ai_response << R"("}})";
    return http_response(body.str());
}

std::string chat_history_response() {
    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) {
            sqlite3_close(db);
        }
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, prompt, response, created_at FROM chat_messages ORDER BY id DESC LIMIT 100;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) {
            body << ",";
        }
        first = false;
        const int id = sqlite3_column_int(stmt, 0);
        const auto* prompt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* response = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        body << R"({"id":)" << id
             << R"(,"prompt":")" << json_escape(prompt ? prompt : "")
             << R"(","response":")" << json_escape(response ? response : "")
             << R"(","created_at":")" << json_escape(created_at ? created_at : "")
             << R"("})";
    }
    body << "]}}";

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return http_response(body.str());
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

std::string create_session_response(const std::string& request) {
    const std::string req_body = request_body(request);
    const std::string title = extract_json_string(req_body, "title");
    if (title.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"title is required"}})",
            "400 Bad Request");
    }

    const std::string id = generate_id("session");
    const std::string now = current_timestamp();

    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    SessionRecord session;
    try {
        SessionRepository session_repository(db);
        session = session_repository.createSession(id, title, now, now);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }
  
     sqlite3_close(db);

     std::ostringstream response_body;

response_body
    << R"({"success":true,"data":{"id":")"
    << json_escape(session.id)
    << R"(","title":")"
    << json_escape(session.title)
    << R"(","created_at":")"
    << json_escape(session.created_at)
    << R"(","updated_at":")"
    << json_escape(session.updated_at)
    << R"("}})";

return http_response(response_body.str());
}

std::string create_log_response(const std::string& request) {
    const std::string body_content = request_body(request);
    const std::string task_id = extract_json_string(body_content, "task_id");
    const std::string type = extract_json_string(body_content, "type");
    const std::string content = extract_json_string(body_content, "content");
    if (task_id.empty() || type.empty() || content.empty()) {
        return http_response(R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id, type and content are required"}})", "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) {
            sqlite3_close(db);
        }
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    sqlite3_int64 id = 0;
    try {
        LogRepository log_repository(db);
        id = log_repository.createLog(task_id, type, content);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }
  
    sqlite3_close(db);
    std::ostringstream response_body;

response_body 
    << R"({"success":true,"data":{"id":)"
    << id
    << R"(,"task_id":")"
    << json_escape(task_id)
    << R"(","type":")"
    << json_escape(type)
    << R"(","content":")"
    << json_escape(content)
    << R"("}})";

return http_response(response_body.str());
}

std::string logs_response(const std::string& request) {
    const std::string task_id = query_parameter(request, "task_id");
    if (task_id.empty()) {
        return http_response(R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})", "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) {
            sqlite3_close(db);
        }
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})", "500 Internal Server Error");
    }

    std::vector<LogRecord> logs;
    try {
        LogRepository log_repository(db);
        logs = log_repository.findByTaskId(task_id);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})", "500 Internal Server Error");
    }

    sqlite3_close(db);
    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    bool first = true;
    for (const LogRecord& log : logs) {
        if (!first) {
            body << ",";
        }
        first = false;
        body << R"({"id":)" << log.id
             << R"(,"task_id":")" << json_escape(log.task_id)
             << R"(","type":")" << json_escape(log.type)
             << R"(","content":")" << json_escape(log.content)
             << R"(","created_at":")" << json_escape(log.created_at)
             << R"("})";
    }
    body << "]}}";

    return http_response(body.str());
}

std::string extract_path_segment(const std::string& request, const std::string& prefix) {
    const std::size_t request_line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, request_line_end);
    const std::size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos) {
        return "";
    }
    const std::size_t path_start = method_end + 1;
    const std::size_t prefix_pos = request_line.find(prefix, path_start);
    if (prefix_pos == std::string::npos) {
        return "";
    }
    const std::size_t segment_start = prefix_pos + prefix.size();
    const std::size_t segment_end = request_line.find(' ', segment_start);
    if (segment_end == std::string::npos) {
        return request_line.substr(segment_start);
    }
    return request_line.substr(segment_start, segment_end - segment_start);
}

std::string create_task_response(const std::string& request) {
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
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    TaskRecord task;
    try {
        TaskRepository task_repository(db);
        task = task_repository.createTask(id, session_id, workspace_id, input, now, now);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    // TODO Sprint 2: 调用 AgentService::runTask(id, session_id, workspace_id, input)
    // AgentResult result = agentService.runTask(id, session_id, workspace_id, input);

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")" << json_escape(task.id)
                  << R"(","session_id":")" << json_escape(task.session_id)
                  << R"(","workspace_id":")" << json_escape(task.workspace_id)
                  << R"(","goal":")" << json_escape(task.goal)
                  << R"(","status":")" << json_escape(task.status) << R"(")"
                  << R"(,"created_at":")" << json_escape(task.created_at)
                  << R"(","updated_at":")" << json_escape(task.updated_at) << R"("}})";
    return http_response(response_body.str());
}

std::string get_task_response(const std::string& request) {
    const std::string task_id = extract_path_segment(request, "/api/v1/tasks/");
    if (task_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"task_id is required"}})",
            "400 Bad Request");
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(database_state.path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::string response;
    try {
        TaskRepository task_repository(db);
        const std::optional<TaskRecord> task = task_repository.findById(task_id);
        if (!task) {
            response = http_response(
                R"({"success":false,"error":{"code":"TASK_NOT_FOUND","message":"任务不存在"}})",
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
            body << R"(","plan":)" << task->plan;
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

std::string not_found_response() {
    const std::string body = R"({"success":false,"error":{"code":"NOT_FOUND","message":"Endpoint not found"}})";
    std::ostringstream response;
    response << "HTTP/1.1 404 Not Found\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;
    return response.str();
}

codepilot::SessionController sessionController;
codepilot::TaskController taskController;
codepilot::WorkspaceController workspaceController;

void handle_client(int client_fd) {
    const std::string request = read_http_request(client_fd);
    if (request.empty()) {
        close(client_fd);
        return;
    }

    std::string response;
    if (request.rfind("OPTIONS ", 0) == 0) {
        response = options_response();
    } else if (request.rfind("GET /api/v1/health ", 0) == 0 || request.rfind("GET /health ", 0) == 0) {
        response = health_response();
    } else if (request.rfind("POST /api/v1/chat ", 0) == 0) {
        response = create_chat_response(request);
    } else if (request.rfind("GET /api/v1/chat/history ", 0) == 0) {
        response = chat_history_response();
    } else if (request.rfind("POST /api/v1/sessions ", 0) == 0) {
        response = sessionController.createSession(request);
    } else if (request.rfind("POST /api/v1/workspaces ", 0) == 0) {
        response = workspaceController.createWorkspace(request);
    } else if (request.rfind("POST /api/v1/logs ", 0) == 0) {
        response = create_log_response(request);
    } else if (request.rfind("GET /api/v1/logs?", 0) == 0 || request.rfind("GET /api/v1/logs ", 0) == 0) {
        response = logs_response(request);
    } else if (request.rfind("POST /api/v1/tasks ", 0) == 0) {
        response = taskController.createTask(request);
    } else if (request.rfind("GET /api/v1/tasks/", 0) == 0) {
        response = taskController.getTask(request);
    } else {
        response = not_found_response();
    }
    send(client_fd, response.data(), response.size(), 0);
    close(client_fd);
}
}

int run_agent_server(const std::string& config_path) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const std::string database_path = extract_storage_path(config_path);
    initialize_database(database_path);

    const int port = 8080;
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Failed to bind 0.0.0.0:" << port << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "Failed to listen on 0.0.0.0:" << port << "\n";
        close(server_fd);
        return 1;
    }

    std::cout << "CodePilot Agent Server starting\n";
    std::cout << "Config: " << config_path << "\n";
    std::cout << "Listening: 0.0.0.0:" << port << "\n";
    std::cout << "Health endpoint: GET /api/v1/health\n";
    std::cout << "SQLite: " << database_state.path << " connected=" << (database_state.connected ? "true" : "false") << "\n";
    if (!database_state.error.empty()) {
        std::cerr << "SQLite error: " << database_state.error << "\n";
    }

    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        const int ready = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(server_fd, &read_fds)) {
            const int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                std::thread(handle_client, client_fd).detach();
            }
        }
    }

    close(server_fd);
    std::cout << "CodePilot Agent Server stopped\n";
    return 0;
}

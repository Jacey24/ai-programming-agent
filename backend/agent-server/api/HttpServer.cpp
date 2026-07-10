#include "api/HttpServer.h"

#include "api/controllers/LogController.h"
#include "api/controllers/FileChangeController.h"
#include "api/controllers/ReplayController.h"
#include "api/controllers/PermissionController.h"
#include "api/controllers/SessionController.h"
#include "api/controllers/TaskController.h"
#include "api/controllers/ToolController.h"
#include "api/controllers/WorkspaceController.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

#include <sqlite3.h>

namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << ch;
        }
    }
    return escaped.str();
}

std::string http_response(const std::string& body, const std::string& status = "200 OK") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;
    return response.str();
}

std::string options_response() {
    return "HTTP/1.1 204 No Content\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type\r\n"
           "Connection: close\r\n"
           "Content-Length: 0\r\n\r\n";
}

std::string not_found_response() {
    return http_response(R"({"success":false,"error":{"code":"NOT_FOUND","message":"Endpoint not found"}})", "404 Not Found");
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
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(ch);
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

} // namespace

namespace codepilot {

HttpServer::HttpServer(HttpServerConfig config) : config_(std::move(config)) {}

int HttpServer::run(const std::atomic_bool& running) {
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
    address.sin_port = htons(static_cast<uint16_t>(config_.port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Failed to bind " << config_.host << ":" << config_.port
                  << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "Failed to listen on " << config_.host << ":" << config_.port << "\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Listening: " << config_.host << ":" << config_.port << "\n";
    std::cout << "Health endpoint: GET /api/v1/health\n";

    while (running.load()) {
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
                std::thread([this, client_fd]() {
                    const std::string request = read_http_request(client_fd);
                    const std::string response = request.empty() ? not_found_response() : handleRequest(request);
                    send(client_fd, response.data(), response.size(), 0);
                    close(client_fd);
                }).detach();
            }
        }
    }

    close(server_fd);
    return 0;
}

std::string HttpServer::handleRequest(const std::string& request) {
    if (request.rfind("OPTIONS ", 0) == 0) {
        return options_response();
    }
    if (request.rfind("GET /api/v1/health ", 0) == 0 || request.rfind("GET /health ", 0) == 0) {
        return healthResponse();
    }
    if (request.rfind("POST /api/v1/chat ", 0) == 0) {
        return createChatResponse(request);
    }
    if (request.rfind("GET /api/v1/chat/history ", 0) == 0) {
        return chatHistoryResponse();
    }
    if (request.rfind("POST /api/v1/sessions ", 0) == 0) {
        SessionController controller(config_.databasePath);
        return controller.createSession(request);
    }
    if (request.rfind("GET /api/v1/sessions/", 0) == 0) {
        SessionController controller(config_.databasePath);
        return controller.getSession(request);
    }
    if (request.rfind("PATCH /api/v1/sessions/", 0) == 0) {
        SessionController controller(config_.databasePath);
        return controller.updateSession(request);
    }
    if (request.rfind("DELETE /api/v1/sessions/", 0) == 0) {
        SessionController controller(config_.databasePath);
        return controller.deleteSession(request);
    }
    if (request.rfind("GET /api/v1/sessions?", 0) == 0 ||
        request.rfind("GET /api/v1/sessions ", 0) == 0) {
        SessionController controller(config_.databasePath);
        return controller.listSessions(request);
    }
    if (request.rfind("POST /api/v1/workspaces ", 0) == 0) {
        WorkspaceController controller(config_.databasePath);
        return controller.createWorkspace(request);
    }
    if (request.rfind("GET /api/v1/workspaces/", 0) == 0) {
        WorkspaceController controller(config_.databasePath);
        return controller.getWorkspace(request);
    }
    if (request.rfind("GET /api/v1/workspaces?", 0) == 0 ||
        request.rfind("GET /api/v1/workspaces ", 0) == 0) {
        WorkspaceController controller(config_.databasePath);
        return controller.listWorkspaces(request);
    }
    if (request.rfind("POST /api/v1/tasks ", 0) == 0) {
        TaskController controller(config_.databasePath);
        return controller.createTask(request);
    }
    if (request.rfind("GET /api/v1/tasks?", 0) == 0 || request.rfind("GET /api/v1/tasks ", 0) == 0) {
        TaskController controller(config_.databasePath);
        return controller.listTasks(request);
    }
    if (request.rfind("GET /api/v1/tasks/", 0) == 0) {
        const std::size_t line_end = request.find("\r\n");
        const std::string request_line = request.substr(0, line_end);
        if (request_line.find("/logs ") != std::string::npos) {
            LogController controller(config_.databasePath);
            return controller.listLogs(request);
        }
        if (request_line.find("/file-changes ") != std::string::npos) {
            FileChangeController controller(config_.databasePath);
            return controller.listFileChanges(request);
        }
        if (request_line.find("/replay ") != std::string::npos) {
            ReplayController controller(config_.databasePath);
            return controller.getReplay(request);
        }
        TaskController controller(config_.databasePath);
        return controller.getTask(request);
    }
    if (request.rfind("POST /api/v1/tasks/", 0) == 0) {
        TaskController controller(config_.databasePath);
        return controller.cancelTask(request);
    }
    if (request.rfind("GET /api/v1/file-changes/", 0) == 0) {
        FileChangeController controller(config_.databasePath);
        return controller.getFileChange(request);
    }
    if (request.rfind("GET /api/v1/tools?", 0) == 0 || request.rfind("GET /api/v1/tools ", 0) == 0) {
        ToolController controller;
        return controller.listTools();
    }
    if (request.rfind("GET /api/v1/tools/", 0) == 0) {
        ToolController controller;
        return controller.getToolDetail(request);
    }
    if (request.rfind("GET /api/v1/permissions/pending ", 0) == 0 ||
        request.rfind("GET /api/v1/permissions?", 0) == 0 ||
        request.rfind("GET /api/v1/permissions ", 0) == 0) {
        PermissionController controller(config_.databasePath);
        return controller.listPermissions(request);
    }
    if (request.rfind("GET /api/v1/permissions/", 0) == 0) {
        PermissionController controller(config_.databasePath);
        return controller.getPermission(request);
    }
    if (request.rfind("POST /api/v1/permissions/", 0) == 0) {
        PermissionController controller(config_.databasePath);
        return controller.handleAction(request);
    }
    return not_found_response();
}

std::string HttpServer::healthResponse() const {
    std::ostringstream body;
    body << R"({"success":true,"data":{"status":"ok","service":"codepilot-agent-server","version":"0.1.0","database":{"type":"sqlite","connected":)"
         << (config_.databaseConnected ? "true" : "false")
         << R"(,"path":")" << json_escape(config_.databasePath) << R"(")";
    if (!config_.databaseError.empty()) {
        body << R"(,"error":")" << json_escape(config_.databaseError) << R"(")";
    }
    body << "}}}";
    return http_response(body.str());
}

std::string HttpServer::createChatResponse(const std::string& request) const {
    const std::string prompt = extract_json_string(request_body(request), "prompt");
    if (prompt.empty()) {
        return http_response(R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"prompt is required"}})", "400 Bad Request");
    }

    constexpr const char* ai_response = "pending development";
    sqlite3* db = nullptr;
    if (sqlite3_open(config_.databasePath.c_str(), &db) != SQLITE_OK) {
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

std::string HttpServer::chatHistoryResponse() const {
    sqlite3* db = nullptr;
    if (sqlite3_open(config_.databasePath.c_str(), &db) != SQLITE_OK) {
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

} // namespace codepilot

#include "SessionController.h"

#include "application/SessionService.h"

#include <chrono>
#include <ctime>
#include <exception>
#include <utility>
#include <sstream>
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
             << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n"
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
    const std::size_t segment_end = request_line.find_first_of("? ", segment_start);
    if (segment_end == std::string::npos) {
        return request_line.substr(segment_start);
    }
    return request_line.substr(segment_start, segment_end - segment_start);
}

} // anonymous namespace

namespace codepilot {

SessionController::SessionController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string SessionController::createSession(const std::string& request) {
    const std::string req_body = request_body(request);
    const std::string title = extract_json_string(req_body, "title");
    if (title.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"title is required"}})",
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

    SessionRecord session;
    try {
        SessionService service(db);
        session = service.createSession(title);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")" << json_escape(session.id)
                  << R"(","title":")" << json_escape(session.title)
                  << R"(","created_at":")" << json_escape(session.created_at)
                  << R"(","updated_at":")" << json_escape(session.updated_at) << R"("}})";
    return http_response(response_body.str());
}

std::string SessionController::listSessions(const std::string& /*request*/) {
    sqlite3* db = nullptr;
    if (sqlite3_open(databasePath_.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) { sqlite3_close(db); }
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error) + R"("}})",
            "500 Internal Server Error");
    }

    std::vector<SessionRecord> sessions;
    try {
        SessionService service(db);
        sessions = service.listSessions();
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" + json_escape(error.what()) + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];
        if (i > 0) {
            body << ",";
        }
        body << R"({"id":")" << json_escape(s.id)
             << R"(","title":")" << json_escape(s.title)
             << R"(","created_at":")" << json_escape(s.created_at)
             << R"(","updated_at":")" << json_escape(s.updated_at)
             << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
}

std::string SessionController::getSession(const std::string& request) {
    const std::string session_id = extract_path_segment(request, "/api/v1/sessions/");
    if (session_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
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
        SessionService service(db);
        const auto session = service.getSessionById(session_id);
        if (!session) {
            response = http_response(
                R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
                "404 Not Found");
            sqlite3_close(db);
            return response;
        }

        std::ostringstream body;
        body << R"({"success":true,"data":{"id":")" << json_escape(session->id)
             << R"(","title":")" << json_escape(session->title)
             << R"(","created_at":")" << json_escape(session->created_at)
             << R"(","updated_at":")" << json_escape(session->updated_at)
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

std::string SessionController::updateSession(const std::string& request) {
    const std::string session_id = extract_path_segment(request, "/api/v1/sessions/");
    if (session_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
            "400 Bad Request");
    }

    const std::string req_body = request_body(request);
    const std::string title = extract_json_string(req_body, "title");
    if (title.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"title is required"}})",
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

    SessionRecord session;
    try {
        SessionService service(db);
        session = service.updateSessionTitle(session_id, title);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        const std::string what = error.what();
        const bool not_found = what.find("not found") != std::string::npos;
        const std::string status = not_found ? "404 Not Found" : "500 Internal Server Error";
        const std::string code   = not_found ? "SESSION_NOT_FOUND" : "DATABASE_ERROR";
        return http_response(
            R"({"success":false,"error":{"code":")" + code + R"(","message":")" + json_escape(what) + R"("}})",
            status);
    }

    sqlite3_close(db);

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")" << json_escape(session.id)
                  << R"(","title":")" << json_escape(session.title)
                  << R"(","created_at":")" << json_escape(session.created_at)
                  << R"(","updated_at":")" << json_escape(session.updated_at) << R"("}})";
    return http_response(response_body.str());
}

std::string SessionController::deleteSession(const std::string& request) {
    const std::string session_id = extract_path_segment(request, "/api/v1/sessions/");
    if (session_id.empty()) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
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

    bool deleted = false;
    try {
        SessionService service(db);
        deleted = service.deleteSession(session_id);
    } catch (const std::exception& error) {
        sqlite3_close(db);
        return http_response(
            R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")"
            + json_escape(error.what())
            + R"("}})",
            "500 Internal Server Error");
    }

    sqlite3_close(db);

    if (!deleted) {
        return http_response(
            R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
            "404 Not Found");
    }

    return http_response(R"({"success":true})");
}

} // namespace codepilot

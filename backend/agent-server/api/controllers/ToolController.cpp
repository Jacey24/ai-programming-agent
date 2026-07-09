#include "ToolController.h"
#include "application/ToolSystem.h"

#include <nlohmann/json.hpp>
#include <sstream>

namespace {

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

} // anonymous namespace

namespace codepilot {

std::string ToolController::listTools() const {
    auto& sys = ToolSystem::getInstance();
    if (!sys.isInitialized()) {
        nlohmann::json body;
        body["success"] = false;
        body["error"]["code"] = "INTERNAL_ERROR";
        body["error"]["message"] = "Tool system not initialized";
        return http_response(body.dump(), "500 Internal Server Error");
    }

    nlohmann::json data = sys.registry().listToolInfo();
    nlohmann::json body;
    body["success"] = true;
    body["data"] = data;
    return http_response(body.dump());
}

std::string ToolController::getToolDetail(const std::string& request) const {
    const std::string tool_name = extract_path_segment(request, "/api/v1/tools/");
    if (tool_name.empty()) {
        nlohmann::json body;
        body["success"] = false;
        body["error"]["code"] = "INVALID_REQUEST";
        body["error"]["message"] = "tool name is required";
        return http_response(body.dump(), "400 Bad Request");
    }

    auto& sys = ToolSystem::getInstance();
    if (!sys.isInitialized()) {
        nlohmann::json body;
        body["success"] = false;
        body["error"]["code"] = "INTERNAL_ERROR";
        body["error"]["message"] = "Tool system not initialized";
        return http_response(body.dump(), "500 Internal Server Error");
    }

    nlohmann::json detail = sys.registry().getToolDetail(tool_name);
    if (detail.empty()) {
        nlohmann::json body;
        body["success"] = false;
        body["error"]["code"] = "TOOL_NOT_FOUND";
        body["error"]["message"] = "tool not found";
        return http_response(body.dump(), "404 Not Found");
    }

    nlohmann::json body;
    body["success"] = true;
    body["data"] = detail;
    return http_response(body.dump());
}

} // namespace codepilot

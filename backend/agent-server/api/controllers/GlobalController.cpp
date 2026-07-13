#include "GlobalController.h"

#include "facade/DataAccessFacade.h"

#include <nlohmann/json.hpp>
#include <sstream>

namespace {

using json = nlohmann::json;

std::string json_escape(const std::string &value) {
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

std::string http_response(const std::string &body,
                          const std::string &status = "200 OK") {
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

std::string request_body(const std::string &request) {
  const std::size_t body_pos = request.find("\r\n\r\n");
  if (body_pos == std::string::npos)
    return "";
  return request.substr(body_pos + 4);
}

std::string extract_path_segment(const std::string &request,
                                 const std::string &prefix) {
  const std::size_t request_line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, request_line_end);
  const std::size_t method_end = request_line.find(' ');
  if (method_end == std::string::npos)
    return "";
  const std::size_t path_start = method_end + 1;
  const std::size_t prefix_pos = request_line.find(prefix, path_start);
  if (prefix_pos == std::string::npos)
    return "";
  const std::size_t segment_start = prefix_pos + prefix.size();
  const std::size_t segment_end =
      request_line.find_first_of("? ", segment_start);
  if (segment_end == std::string::npos) {
    return request_line.substr(segment_start);
  }
  return request_line.substr(segment_start, segment_end - segment_start);
}

} // namespace

namespace codepilot {

GlobalController::GlobalController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string GlobalController::createGlobal(const std::string &request) {
  const std::string body_content = request_body(request);
  json body = json::parse(body_content, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"request body must be valid JSON"}})",
        "400 Bad Request");
  }

  const std::string name = body.value("name", "未命名");
  const std::string description = body.value("description", "");

  if (!DataAccessFacade::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto record =
        DataAccessFacade::getInstance().createGlobal(name, description);
    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")"
                  << json_escape(record.id) << R"(","name":")"
                  << json_escape(record.name) << R"(","description":")"
                  << json_escape(record.description) << R"(","created_at":")"
                  << json_escape(record.created_at) << R"(","updated_at":")"
                  << json_escape(record.updated_at) << R"("}})";
    return http_response(response_body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string GlobalController::getGlobal(const std::string &request) {
  const std::string global_id =
      extract_path_segment(request, "/api/v1/globals/");
  if (global_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"global_id is required"}})",
        "400 Bad Request");
  }

  if (!DataAccessFacade::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto global = DataAccessFacade::getInstance().getGlobal(global_id);
    if (!global) {
      return http_response(
          R"({"success":false,"error":{"code":"NOT_FOUND","message":"global not found"}})",
          "404 Not Found");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(global->id)
         << R"(","name":")" << json_escape(global->name)
         << R"(","description":")" << json_escape(global->description)
         << R"(","created_at":")" << json_escape(global->created_at)
         << R"(","updated_at":")" << json_escape(global->updated_at)
         << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string GlobalController::listGlobals(const std::string & /*request*/) {
  if (!DataAccessFacade::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto globals = DataAccessFacade::getInstance().listGlobals();
    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < globals.size(); ++i) {
      const auto &g = globals[i];
      if (i > 0)
        body << ",";
      body << R"({"id":")" << json_escape(g.id) << R"(","name":")"
           << json_escape(g.name) << R"(","description":")"
           << json_escape(g.description) << R"(","created_at":")"
           << json_escape(g.created_at) << R"(","updated_at":")"
           << json_escape(g.updated_at) << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string GlobalController::getGlobalContext(const std::string &request) {
  const std::string full_segment =
      extract_path_segment(request, "/api/v1/globals/");
  const std::string suffix = "/context";
  if (full_segment.size() <= suffix.size() ||
      full_segment.compare(full_segment.size() - suffix.size(), suffix.size(),
                           suffix) != 0) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"path must end with /context"}})",
        "400 Bad Request");
  }
  const std::string global_id =
      full_segment.substr(0, full_segment.size() - suffix.size());

  if (!DataAccessFacade::getInstance().isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto contexts = DataAccessFacade::getInstance().getGlobalContext(global_id);
    std::ostringstream body;
    body << R"({"success":true,"data":{"global_id":")" << json_escape(global_id)
         << R"(","items":[)";
    for (std::size_t i = 0; i < contexts.size(); ++i) {
      const auto &ctx = contexts[i];
      if (i > 0)
        body << ",";
      body << R"({"id":)" << ctx.id << R"(,"global_id":")"
           << json_escape(ctx.global_id) << R"(","source_task_id":")"
           << json_escape(ctx.source_task_id) << R"(","type":")"
           << json_escape(ctx.type) << R"(","content":")"
           << json_escape(ctx.content) << R"(","created_at":")"
           << json_escape(ctx.created_at) << R"("})";
    }
    body << "]}}";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

} // namespace codepilot
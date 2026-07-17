#include "SessionController.h"
#include "api/controllers/HttpUtils.h"
#include "facade/DataAccessFacade.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

namespace codepilot {

SessionController::SessionController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string SessionController::createSession(const std::string &request) {
  const std::string req_body = request_body(request);
  const std::string title = extract_json_string(req_body, "title");
  const std::string workspace_id =
      extract_json_string(req_body, "workspace_id");
  if (title.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"title is required"}})",
        "400 Bad Request");
  }
  if (workspace_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id is required"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    if (!facade.getWorkspace(workspace_id)) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
    }
    auto session = facade.createSession(title, workspace_id);
    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")"
                  << json_escape(session.id) << R"(","workspace_id":")"
                  << json_escape(session.workspace_id) << R"(","title":")"
                  << json_escape(session.title) << R"(","alias":")"
                  << json_escape(session.alias) << R"(","created_at":")"
                  << json_escape(session.created_at) << R"(","updated_at":")"
                  << json_escape(session.updated_at) << R"("}})";
    return http_response(response_body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string SessionController::updateSession(const std::string &request) {
  const std::string session_id =
      extract_path_segment(request, "/api/v1/sessions/");
  if (session_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
        "400 Bad Request");
  }

  const std::string req_body = request_body(request);
  const std::string title = extract_json_string(req_body, "title");
  const std::string alias = extract_json_string(req_body, "alias");
  const std::string workspace_id =
      extract_json_string(req_body, "workspace_id");

  if (title.empty() && alias.empty() && workspace_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"at least one of title, alias, workspace_id is required"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    bool ok = facade.updateSession(session_id, title, alias, workspace_id);
    if (!ok) {
      return http_response(
          R"({"success":false,"error":{"code":"NOT_FOUND","message":"session not found or could not be updated"}})",
          "404 Not Found");
    }

    auto updated = facade.getSession(session_id);
    if (!updated) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found after update"}})",
          "404 Not Found");
    }

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")"
                  << json_escape(updated->id) << R"(","title":")"
                  << json_escape(updated->title) << R"(","alias":")"
                  << json_escape(updated->alias) << R"(","workspace_id":")"
                  << json_escape(updated->workspace_id) << R"(","created_at":")"
                  << json_escape(updated->created_at) << R"(","updated_at":")"
                  << json_escape(updated->updated_at) << R"("}})";
    return http_response(response_body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string SessionController::listSessions(const std::string & /*request*/) {
  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto sessions = facade.listSessions();
    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < sessions.size(); ++i) {
      const auto &s = sessions[i];
      if (i > 0) {
        body << ",";
      }
      body << R"({"id":")" << json_escape(s.id) << R"(","title":")"
           << json_escape(s.title) << R"(","alias":")" << json_escape(s.alias)
           << R"(","workspace_id":")" << json_escape(s.workspace_id)
           << R"(","created_at":")" << json_escape(s.created_at)
           << R"(","updated_at":")" << json_escape(s.updated_at) << R"("})";
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

std::string SessionController::getSession(const std::string &request) {
  const std::string session_id =
      extract_path_segment(request, "/api/v1/sessions/");
  if (session_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto session = facade.getSession(session_id);
    if (!session) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
          "404 Not Found");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(session->id)
         << R"(","title":")" << json_escape(session->title) << R"(","alias":")"
         << json_escape(session->alias) << R"(","workspace_id":")"
         << json_escape(session->workspace_id) << R"(","created_at":")"
         << json_escape(session->created_at) << R"(","updated_at":")"
         << json_escape(session->updated_at) << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string SessionController::listMessages(const std::string &request) {
  std::string session_id =
      extract_path_segment(request, "/api/v1/sessions/");
  const std::size_t suffix = session_id.find("/messages");
  if (suffix != std::string::npos) {
    session_id.resize(suffix);
  }
  if (session_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    if (!facade.getSession(session_id)) {
      return http_response(
          R"({"success":false,"error":{"code":"SESSION_NOT_FOUND","message":"session not found"}})",
          "404 Not Found");
    }
    const auto messages = facade.listMessagesBySession(session_id);
    std::ostringstream body;
    body << R"({"success":true,"data":{"session_id":")"
         << json_escape(session_id) << R"(","items":[)";
    for (std::size_t i = 0; i < messages.size(); ++i) {
      const auto &message = messages[i];
      if (i > 0) {
        body << ',';
      }
      body << R"({"id":")" << json_escape(message.id)
           << R"(","session_id":")" << json_escape(message.session_id)
           << R"(","task_id":)";
      if (message.task_id) {
        body << '"' << json_escape(*message.task_id) << '"';
      } else {
        body << "null";
      }
      body << R"(,"role":")" << json_escape(message.role)
           << R"(","message_type":")" << json_escape(message.message_type)
           << R"(","content":")" << json_escape(message.content)
           << R"(","sequence_no":)" << message.sequence_no
           << R"(,"source_event_id":)";
      if (message.source_event_id) {
        body << '"' << json_escape(*message.source_event_id) << '"';
      } else {
        body << "null";
      }
      body << R"(,"created_at":")" << json_escape(message.created_at)
           << R"("})";
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

std::string SessionController::deleteSession(const std::string &request) {
  const std::string session_id =
      extract_path_segment(request, "/api/v1/sessions/");
  if (session_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"session_id is required"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    bool ok = facade.deleteSession(session_id);
    if (!ok) {
      return http_response(
          R"({"success":false,"error":{"code":"NOT_FOUND","message":"session not found or could not be deleted"}})",
          "404 Not Found");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(session_id)
         << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

} // namespace codepilot

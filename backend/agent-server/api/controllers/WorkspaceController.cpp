#include "WorkspaceController.h"
#include "api/controllers/HttpUtils.h"
#include "application/ToolSystem.h"
#include "facade/DataAccessFacade.h"
#include "infrastructure/filesystem/WorkspaceManager.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <shobjidl.h>
#include <windows.h>

#pragma comment(lib, "ole32.lib")
#endif

namespace {

std::optional<std::string>
validate_permissions_config(const nlohmann::json &config) {
  if (!config.is_object())
    return "permissions_config must be an object";
  auto &tools = codepilot::ToolSystem::getInstance();
  for (auto it = config.begin(); it != config.end(); ++it) {
    if (!it.value().is_string())
      return "permission policy for [" + it.key() + "] must be a string";
    const std::string action = it.value().get<std::string>();
    if (action != "ask" && action != "auto_approve" && action != "deny")
      return "unsupported permission policy for [" + it.key() + "]";
    if (it.key() == "*" && action == "auto_approve")
      return "wildcard auto_approve is not allowed";
    if (it.key() != "*" && tools.isInitialized() &&
        !tools.registry().hasTool(it.key()))
      return "unknown tool permission ID: " + it.key();
    if (action == "auto_approve" && tools.isInitialized()) {
      const auto detail = tools.registry().getToolDetail(it.key());
      const std::string risk = detail.value("risk_level", "");
      const std::string group = detail.value("group", "");
      if (risk != "safe" || group == "shell")
        return "high-risk tool cannot be auto-approved: " + it.key();
    }
  }
  return std::nullopt;
}

bool is_workspace_directory(const std::string &path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error;
}

#ifdef _WIN32
std::string utf8_from_wide(const wchar_t *value) {
  if (!value) {
    return {};
  }
  const int length =
      WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) {
    return {};
  }
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

std::optional<std::string> select_local_directory() {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool shouldUninitialize = SUCCEEDED(init);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    return std::nullopt;
  }

  IFileOpenDialog *dialog = nullptr;
  std::optional<std::string> selected;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                         FOS_PATHMUSTEXIST);
    }
    if (SUCCEEDED(dialog->Show(nullptr))) {
      IShellItem *item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          selected = utf8_from_wide(path);
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
    dialog->Release();
  }
  if (shouldUninitialize) {
    CoUninitialize();
  }
  return selected;
}
#endif

} // anonymous namespace

namespace codepilot {

WorkspaceController::WorkspaceController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string
WorkspaceController::selectLocalDirectory(const std::string & /*request*/) {
#ifdef _WIN32
  const auto path = select_local_directory();
  if (!path) {
    return http_response(R"({"success":true,"data":{"cancelled":true}})");
  }
  const auto name = std::filesystem::path(*path).filename().u8string();
  const std::string utf8Name(reinterpret_cast<const char *>(name.data()),
                             name.size());
  return http_response(R"({"success":true,"data":{"cancelled":false,"name":")" +
                       json_escape(utf8Name) + R"(","path":")" +
                       json_escape(*path) + R"("}})");
#else
  return http_response(
      R"({"success":false,"error":{"code":"UNSUPPORTED_PLATFORM","message":"native directory selection is only available on Windows"}})",
      "501 Not Implemented");
#endif
}

std::string WorkspaceController::createWorkspace(const std::string &request) {
  const std::string req_body = request_body(request);
  const std::string name = extract_json_string(req_body, "name");
  const std::string path = extract_json_string(req_body, "path");
  if (name.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"name is required"}})",
        "400 Bad Request");
  }
  if (path.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_WORKSPACE_PATH","message":"workspace path is required"}})",
        "400 Bad Request");
  }

  const std::filesystem::path workspace_path(path);
  std::error_code filesystem_error;
  const bool path_exists =
      std::filesystem::exists(workspace_path, filesystem_error);
  if (filesystem_error) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_WORKSPACE_PATH","message":"failed to access workspace path: )" +
            json_escape(filesystem_error.message()) + R"("}})",
        "400 Bad Request");
  }
  if (!path_exists) {
    return http_response(
        R"({"success":false,"error":{"code":"WORKSPACE_PATH_NOT_FOUND","message":"workspace path does not exist"}})",
        "404 Not Found");
  }

  const bool path_is_directory =
      std::filesystem::is_directory(workspace_path, filesystem_error);
  if (filesystem_error) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_WORKSPACE_PATH","message":"failed to inspect workspace path: )" +
            json_escape(filesystem_error.message()) + R"("}})",
        "400 Bad Request");
  }
  if (!path_is_directory) {
    return http_response(
        R"({"success":false,"error":{"code":"WORKSPACE_PATH_NOT_DIRECTORY","message":"workspace path must be a directory"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  // Extract permissions_config as raw JSON string
  std::string permissions_config = "{}";
  try {
    auto bodyJson = nlohmann::json::parse(req_body, nullptr, false);
    if (!bodyJson.is_discarded() && bodyJson.is_object() &&
        bodyJson.contains("permissions_config")) {
      if (const auto error =
              validate_permissions_config(bodyJson["permissions_config"])) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_PERMISSION_CONFIG","message":")" +
                json_escape(*error) + R"("}})",
            "400 Bad Request");
      }
      permissions_config = bodyJson["permissions_config"].dump();
    }
  } catch (...) {
    // 使用空默认值
  }

  try {
    auto rec = facade.createWorkspace(name, path, permissions_config);

    // Create the per-workspace runtime immediately
    WorkspaceManager::getInstance().getOrCreate(rec.id, rec.path);

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")" << json_escape(rec.id)
                  << R"(","name":")" << json_escape(rec.name) << R"(","path":")"
                  << json_escape(rec.path) << R"(","permissions_config":)"
                  << rec.permissions_config << R"(,"created_at":")"
                  << json_escape(rec.created_at) << R"("}})";
    return http_response(response_body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string WorkspaceController::updateWorkspace(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
  if (workspace_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id is required"}})",
        "400 Bad Request");
  }

  const std::string req_body = request_body(request);
  const std::string name = extract_json_string(req_body, "name");
  const std::string description = extract_json_string(req_body, "description");
  const std::string path = extract_json_string(req_body, "path");

  if (name.empty() && description.empty() && path.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"at least one of name, description is required"}})",
        "400 Bad Request");
  }
  if (!path.empty() && !is_workspace_directory(path)) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_WORKSPACE_PATH","message":"path must be an existing directory"}})",
        "400 Bad Request");
  }

  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  // Extract permissions_config as raw JSON string
  std::string permissions_config;
  try {
    auto bodyJson = nlohmann::json::parse(req_body, nullptr, false);
    if (!bodyJson.is_discarded() && bodyJson.is_object() &&
        bodyJson.contains("permissions_config")) {
      if (const auto error =
              validate_permissions_config(bodyJson["permissions_config"])) {
        return http_response(
            R"({"success":false,"error":{"code":"INVALID_PERMISSION_CONFIG","message":")" +
                json_escape(*error) + R"("}})",
            "400 Bad Request");
      }
      permissions_config = bodyJson["permissions_config"].dump();
    }
  } catch (...) {
    // 保持空
  }

  try {
    bool ok = facade.updateWorkspace(workspace_id, name, description, path,
                                     permissions_config);
    if (!ok) {
      return http_response(
          R"({"success":false,"error":{"code":"NOT_FOUND","message":"workspace not found or could not be updated"}})",
          "404 Not Found");
    }

    auto updated = facade.getWorkspace(workspace_id);
    if (!updated) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found after update"}})",
          "404 Not Found");
    }

    // A path update migrates the existing runtime under its execution lock
    WorkspaceManager::getInstance().getOrCreate(updated->id, updated->path);

    std::ostringstream response_body;
    response_body << R"({"success":true,"data":{"id":")"
                  << json_escape(updated->id) << R"(","name":")"
                  << json_escape(updated->name) << R"(","path":")"
                  << json_escape(updated->path) << R"(","description":")"
                  << json_escape(updated->description)
                  << R"(","permissions_config":)" << updated->permissions_config
                  << R"(,"created_at":")" << json_escape(updated->created_at)
                  << R"("}})";
    return http_response(response_body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string
WorkspaceController::listWorkspaces(const std::string & /*request*/) {
  auto &facade = DataAccessFacade::getInstance();
  if (!facade.isInitialized()) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":"DataAccessFacade not initialized"}})",
        "500 Internal Server Error");
  }

  try {
    auto workspaces = facade.listWorkspaces();
    std::ostringstream body;
    body << R"({"success":true,"data":{"items":[)";
    for (std::size_t i = 0; i < workspaces.size(); ++i) {
      const auto &ws = workspaces[i];
      if (i > 0) {
        body << ",";
      }
      body << R"({"id":")" << json_escape(ws.id) << R"(","name":")"
           << json_escape(ws.name) << R"(","path":")" << json_escape(ws.path)
           << R"(","permissions_config":)" << ws.permissions_config
           << R"(,"created_at":")" << json_escape(ws.created_at) << R"("})";
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

std::string WorkspaceController::getWorkspace(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
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
    auto ws = facade.getWorkspace(workspace_id);
    if (!ws) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(ws->id)
         << R"(","name":")" << json_escape(ws->name) << R"(","path":")"
         << json_escape(ws->path) << R"(","permissions_config":)"
         << ws->permissions_config << R"(,"created_at":")"
         << json_escape(ws->created_at) << R"("}})";
    return http_response(body.str());
  } catch (const std::exception &error) {
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }
}

std::string WorkspaceController::listSessions(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
  // strip trailing /sessions
  std::string clean_id = workspace_id;
  const std::string suffix = "/sessions";
  if (clean_id.size() > suffix.size() &&
      clean_id.compare(clean_id.size() - suffix.size(), suffix.size(),
                       suffix) == 0) {
    clean_id = clean_id.substr(0, clean_id.size() - suffix.size());
  }
  if (clean_id.empty()) {
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
    if (!facade.getWorkspace(clean_id)) {
      return http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
    }
    auto sessions = facade.listSessionsByWorkspace(clean_id);
    std::ostringstream body;
    body << R"({"success":true,"data":{"workspace_id":")"
         << json_escape(clean_id) << R"(","items":[)";
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

std::string WorkspaceController::deleteWorkspace(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
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
    bool ok = facade.deleteWorkspace(workspace_id);
    if (!ok) {
      return http_response(
          R"({"success":false,"error":{"code":"NOT_FOUND","message":"workspace not found or could not be deleted"}})",
          "404 Not Found");
    }

    WorkspaceManager::getInstance().invalidate(workspace_id);

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(workspace_id)
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

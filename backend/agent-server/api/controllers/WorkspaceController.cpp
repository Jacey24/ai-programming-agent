#include "WorkspaceController.h"
#include "infrastructure/storage/SqliteConnection.h"

#include "application/WorkspaceService.h"
#include "infrastructure/filesystem/WorkspaceManager.h"

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <sqlite3.h>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#endif

namespace {

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

std::string current_timestamp() {
  const auto now = std::time(nullptr);
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return buf;
}

std::string generate_id(const std::string &prefix) {
  return prefix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string extract_json_string(const std::string &body,
                                const std::string &key) {
  const std::string marker = "\"" + key + "\"";
  const std::size_t marker_pos = body.find(marker);
  if (marker_pos == std::string::npos)
    return "";

  const std::size_t colon_pos = body.find(':', marker_pos + marker.size());
  const std::size_t first_quote = body.find('"', colon_pos);
  if (colon_pos == std::string::npos || first_quote == std::string::npos)
    return "";

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
    if (ch == '"')
      break;
    value.push_back(ch);
  }
  return value;
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

bool is_workspace_directory(const std::string &path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error;
}

#ifdef _WIN32
std::string utf8_from_wide(const wchar_t *value) {
  if (!value) {
    return {};
  }
  const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                         nullptr, nullptr);
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
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
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
                       json_escape(utf8Name) +
                       R"(","path":")" + json_escape(*path) + R"("}})");
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

  const std::string id = generate_id("workspace");
  const std::string now = current_timestamp();

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  const char *sql = "INSERT INTO workspaces (id, name, path, created_at) "
                    "VALUES (?, ?, ?, ?);";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (step_result != SQLITE_DONE) {
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  sqlite3_close(db);

  // Create the per-workspace runtime immediately.  Future tasks select this
  // runtime by workspace_id instead of inheriting the server bootstrap path.
  WorkspaceManager::getInstance().getOrCreate(id, path);

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")" << json_escape(id)
                << R"(","name":")" << json_escape(name) << R"(","path":")"
                << json_escape(path) << R"(","created_at":")" << now
                << R"("}})";
  return http_response(response_body.str());
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

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  // Build dynamic UPDATE
  std::ostringstream sql;
  sql << "UPDATE workspaces SET ";
  std::vector<std::string> bindValues;
  bool first = true;

  if (!name.empty()) {
    sql << "name = ?";
    bindValues.push_back(name);
    first = false;
  }
  if (!description.empty()) {
    if (!first)
      sql << ", ";
    sql << "description = ?";
    bindValues.push_back(description);
    first = false;
  }
  if (!path.empty()) {
    if (!first)
      sql << ", ";
    sql << "path = ?";
    bindValues.push_back(path);
    first = false;
  }
  sql << " WHERE id = ?;";

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr) !=
      SQLITE_OK) {
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  for (size_t i = 0; i < bindValues.size(); ++i) {
    sqlite3_bind_text(stmt, static_cast<int>(i + 1), bindValues[i].c_str(), -1,
                      SQLITE_TRANSIENT);
  }
  sqlite3_bind_text(stmt, static_cast<int>(bindValues.size() + 1),
                    workspace_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (step_result != SQLITE_DONE) {
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"NOT_FOUND","message":"workspace not found or could not be updated"}})",
        "404 Not Found");
  }

  // Read back updated record
  WorkspaceService service(db);
  const auto updated = service.getWorkspaceById(workspace_id);
  sqlite3_close(db);

  if (!updated) {
    return http_response(
        R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found after update"}})",
        "404 Not Found");
  }

  // A path update migrates the existing runtime under its execution lock;
  // selecting this workspace afterwards uses the new working directory.
  WorkspaceManager::getInstance().getOrCreate(updated->id, updated->path);

  std::ostringstream response_body;
  response_body << R"({"success":true,"data":{"id":")"
                << json_escape(updated->id) << R"(","name":")"
                << json_escape(updated->name) << R"(","path":")"
                << json_escape(updated->path) << R"(","description":")"
                << json_escape(updated->description) << R"(","created_at":")"
                << json_escape(updated->created_at) << R"("}})";
  return http_response(response_body.str());
}

std::string
WorkspaceController::listWorkspaces(const std::string & /*request*/) {
  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  std::vector<WorkspaceRecord> workspaces;
  try {
    WorkspaceService service(db);
    workspaces = service.listWorkspaces();
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  sqlite3_close(db);

  std::ostringstream body;
  body << R"({"success":true,"data":{"items":[)";
  for (std::size_t i = 0; i < workspaces.size(); ++i) {
    const auto &ws = workspaces[i];
    if (i > 0) {
      body << ",";
    }
    body << R"({"id":")" << json_escape(ws.id) << R"(","name":")"
         << json_escape(ws.name) << R"(","path":")" << json_escape(ws.path)
         << R"(","created_at":")" << json_escape(ws.created_at) << R"("})";
  }
  body << "]}}";
  return http_response(body.str());
}

std::string WorkspaceController::getWorkspace(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
  if (workspace_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id is required"}})",
        "400 Bad Request");
  }

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  std::string response;
  try {
    WorkspaceService service(db);
    const auto ws = service.getWorkspaceById(workspace_id);
    if (!ws) {
      response = http_response(
          R"({"success":false,"error":{"code":"WORKSPACE_NOT_FOUND","message":"workspace not found"}})",
          "404 Not Found");
      sqlite3_close(db);
      return response;
    }

    std::ostringstream body;
    body << R"({"success":true,"data":{"id":")" << json_escape(ws->id)
         << R"(","name":")" << json_escape(ws->name) << R"(","path":")"
         << json_escape(ws->path) << R"(","created_at":")"
         << json_escape(ws->created_at) << R"("}})";
    response = http_response(body.str());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error.what()) + R"("}})",
        "500 Internal Server Error");
  }

  sqlite3_close(db);
  return response;
}

std::string WorkspaceController::deleteWorkspace(const std::string &request) {
  const std::string workspace_id =
      extract_path_segment(request, "/api/v1/workspaces/");
  if (workspace_id.empty()) {
    return http_response(
        R"({"success":false,"error":{"code":"INVALID_REQUEST","message":"workspace_id is required"}})",
        "400 Bad Request");
  }

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  const char *sql = "DELETE FROM workspaces WHERE id = ?;";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    return http_response(
        R"({"success":false,"error":{"code":"DATABASE_ERROR","message":")" +
            json_escape(error) + R"("}})",
        "500 Internal Server Error");
  }

  sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  if (step_result != SQLITE_DONE) {
    return http_response(
        R"({"success":false,"error":{"code":"NOT_FOUND","message":"workspace not found or could not be deleted"}})",
        "404 Not Found");
  }

  WorkspaceManager::getInstance().invalidate(workspace_id);

  std::ostringstream body;
  body << R"({"success":true,"data":{"id":")" << json_escape(workspace_id)
       << R"("}})";
  return http_response(body.str());
}

} // namespace codepilot

#include "WorkspaceFileController.h"
#include "infrastructure/storage/SqliteConnection.h"

#include "application/WorkspaceService.h"
#include "infrastructure/filesystem/Workspace.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>


#include <sqlite3.h>

namespace {

using json = nlohmann::json;

constexpr int64_t kMaxReadableFileSize = 10 * 1024 * 1024;

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

std::string error_response(const std::string &code, const std::string &message,
                           const std::string &status = "400 Bad Request") {
  json body;
  body["success"] = false;
  body["error"] = {{"code", code}, {"message", message}};
  return http_response(body.dump(), status);
}

std::string request_target(const std::string &request) {
  const std::size_t request_line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, request_line_end);
  const std::size_t method_end = request_line.find(' ');
  if (method_end == std::string::npos) {
    return "";
  }
  const std::size_t target_start = method_end + 1;
  const std::size_t target_end = request_line.find(' ', target_start);
  // If no HTTP version, treat end of line as path end
  const std::size_t effective_end =
      (target_end == std::string::npos) ? request_line.size() : target_end;
  return request_line.substr(target_start, effective_end - target_start);
}

std::string url_decode(const std::string &input) {
  std::ostringstream decoded;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const char ch = input[i];
    if (ch == '+') {
      decoded << ' ';
      continue;
    }
    if (ch == '%' && i + 2 < input.size() &&
        std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
      const std::string hex = input.substr(i + 1, 2);
      decoded << static_cast<char>(std::stoi(hex, nullptr, 16));
      i += 2;
      continue;
    }
    decoded << ch;
  }
  return decoded.str();
}

std::string extract_query_string(const std::string &request,
                                 const std::string &key) {
  const std::string target = request_target(request);
  const std::size_t query_start = target.find('?');
  if (query_start == std::string::npos) {
    return "";
  }
  std::size_t pos = query_start + 1;
  while (pos < target.size()) {
    const std::size_t next = target.find('&', pos);
    const std::string part = target.substr(
        pos, next == std::string::npos ? std::string::npos : next - pos);
    const std::size_t equals = part.find('=');
    const std::string name =
        url_decode(equals == std::string::npos ? part : part.substr(0, equals));
    if (name == key) {
      return url_decode(equals == std::string::npos ? ""
                                                    : part.substr(equals + 1));
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  return "";
}

int extract_query_int(const std::string &request, const std::string &key,
                      int fallback) {
  const std::string raw = extract_query_string(request, key);
  if (raw.empty()) {
    return fallback;
  }
  try {
    return std::stoi(raw);
  } catch (...) {
    return fallback;
  }
}

std::string extract_workspace_id(const std::string &request) {
  const std::string target = request_target(request);
  const std::size_t query_start = target.find('?');
  const std::string path = target.substr(0, query_start);
  const std::string prefix = "/api/v1/workspaces/";
  const std::size_t prefix_pos = path.find(prefix);
  if (prefix_pos == std::string::npos) {
    return "";
  }
  const std::size_t id_start = prefix_pos + prefix.size();
  const std::size_t id_end = path.find('/', id_start);
  if (id_end == std::string::npos || id_end == id_start) {
    return "";
  }
  return url_decode(path.substr(id_start, id_end - id_start));
}

std::string language_from_path(const std::string &path) {
  const std::string ext = std::filesystem::path(path).extension().string();
  if (ext == ".py")
    return "python";
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
      ext == ".hpp")
    return "cpp";
  if (ext == ".ts" || ext == ".tsx")
    return "typescript";
  if (ext == ".js" || ext == ".jsx")
    return "javascript";
  if (ext == ".json")
    return "json";
  if (ext == ".md")
    return "markdown";
  if (ext == ".css")
    return "css";
  if (ext == ".html" || ext == ".htm")
    return "html";
  if (ext == ".yml" || ext == ".yaml")
    return "yaml";
  return "plaintext";
}

std::string filename_from_path(const std::string &path) {
  return std::filesystem::path(path).filename().string();
}

} // namespace

namespace codepilot {

WorkspaceFileController::WorkspaceFileController(std::string database_path)
    : databasePath_(std::move(database_path)) {}

std::string WorkspaceFileController::getTree(const std::string &request) {
  const std::string workspace_id = extract_workspace_id(request);
  if (workspace_id.empty()) {
    return error_response("INVALID_REQUEST", "workspace_id is required");
  }
  const std::string relative_path = extract_query_string(request, "path");
  const int depth =
      std::max(1, std::min(extract_query_int(request, "depth", 4), 16));

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return error_response("DATABASE_ERROR", error, "500 Internal Server Error");
  }

  try {
    WorkspaceService service(db);
    const auto record = service.getWorkspaceById(workspace_id);
    if (!record) {
      sqlite3_close(db);
      return error_response("WORKSPACE_NOT_FOUND", "workspace not found",
                            "404 Not Found");
    }

    Workspace workspace(record->path);
    if (!relative_path.empty() && !workspace.isPathSafe(relative_path)) {
      sqlite3_close(db);
      return error_response("INVALID_PATH", "path is not allowed");
    }

    json items = json::array();
    for (const auto &entry : workspace.listFiles(relative_path, depth)) {
      items.push_back({{"name", entry.name},
                       {"path", entry.path},
                       {"type", entry.type},
                       {"size", entry.size}});
    }

    json body;
    body["success"] = true;
    body["data"] = {{"workspace_id", workspace_id},
                    {"root", relative_path},
                    {"items", items}};
    sqlite3_close(db);
    return http_response(body.dump());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return error_response("DATABASE_ERROR", error.what(),
                          "500 Internal Server Error");
  }
}

std::string
WorkspaceFileController::getFileContent(const std::string &request) {
  const std::string workspace_id = extract_workspace_id(request);
  const std::string relative_path = extract_query_string(request, "path");
  if (workspace_id.empty() || relative_path.empty()) {
    return error_response("INVALID_REQUEST",
                          "workspace_id and path are required");
  }

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) {
      sqlite3_close(db);
    }
    return error_response("DATABASE_ERROR", error, "500 Internal Server Error");
  }

  try {
    WorkspaceService service(db);
    const auto record = service.getWorkspaceById(workspace_id);
    if (!record) {
      sqlite3_close(db);
      return error_response("WORKSPACE_NOT_FOUND", "workspace not found",
                            "404 Not Found");
    }

    Workspace workspace(record->path);
    if (!workspace.isPathSafe(relative_path)) {
      sqlite3_close(db);
      return error_response("INVALID_PATH", "path is not allowed");
    }

    const std::string full_path = workspace.resolvePath(relative_path);
    if (!std::filesystem::exists(full_path) ||
        !std::filesystem::is_regular_file(full_path)) {
      sqlite3_close(db);
      return error_response("FILE_NOT_FOUND", "file not found",
                            "404 Not Found");
    }

    const int64_t size = workspace.getFileSize(relative_path);
    if (size < 0) {
      sqlite3_close(db);
      return error_response("FILE_NOT_FOUND", "file not found",
                            "404 Not Found");
    }
    if (size > kMaxReadableFileSize) {
      sqlite3_close(db);
      return error_response("FILE_TOO_LARGE",
                            "file exceeds maximum readable size",
                            "413 Payload Too Large");
    }
    if (workspace.isBinaryFile(relative_path)) {
      sqlite3_close(db);
      return error_response("BINARY_FILE", "binary files cannot be read",
                            "415 Unsupported Media Type");
    }

    const std::string content = workspace.readFile(relative_path, 1, -1);
    json body;
    body["success"] = true;
    body["data"] = {{"path", relative_path},
                    {"name", filename_from_path(relative_path)},
                    {"language", language_from_path(relative_path)},
                    {"content", content},
                    {"size", size},
                    {"readonly", true}};
    sqlite3_close(db);
    return http_response(body.dump());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return error_response("DATABASE_ERROR", error.what(),
                          "500 Internal Server Error");
  }
}

} // namespace codepilot

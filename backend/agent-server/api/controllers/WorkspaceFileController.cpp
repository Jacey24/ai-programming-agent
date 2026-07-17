#include "WorkspaceFileController.h"
#include "infrastructure/storage/SqliteConnection.h"

#include "application/WorkspaceService.h"
#include "infrastructure/filesystem/Workspace.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>
#endif

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
           << "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
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

std::string request_body(const std::string &request) {
  const std::size_t separator = request.find("\r\n\r\n");
  return separator == std::string::npos ? "" : request.substr(separator + 4);
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
  const std::string name = std::filesystem::path(path).filename().string();
  const std::string ext = std::filesystem::path(path).extension().string();
  if (name == "CMakeLists.txt" || ext == ".cmake")
    return "cmake";
  if (ext == ".py")
    return "python";
  if (ext == ".c")
    return "c";
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
  if (ext == ".xml")
    return "xml";
  if (ext == ".ini")
    return "ini";
  if (ext == ".toml")
    return "toml";
  return "plaintext";
}

std::string filename_from_path(const std::string &path) {
  return std::filesystem::path(path).filename().string();
}

bool is_known_binary_path(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext == ".exe" || ext == ".dll" || ext == ".pdb" ||
         ext == ".obj" || ext == ".lib" || ext == ".zip" ||
         ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".gif" || ext == ".pdf";
}

bool is_valid_utf8(const std::string &value) {
  for (std::size_t i = 0; i < value.size();) {
    const unsigned char lead = static_cast<unsigned char>(value[i]);
    std::size_t count = 0;
    if (lead <= 0x7f) count = 1;
    else if (lead >= 0xc2 && lead <= 0xdf) count = 2;
    else if (lead >= 0xe0 && lead <= 0xef) count = 3;
    else if (lead >= 0xf0 && lead <= 0xf4) count = 4;
    else return false;
    if (i + count > value.size()) return false;
    for (std::size_t offset = 1; offset < count; ++offset) {
      const unsigned char ch = static_cast<unsigned char>(value[i + offset]);
      if ((ch & 0xc0) != 0x80) return false;
    }
    if ((lead == 0xe0 && static_cast<unsigned char>(value[i + 1]) < 0xa0) ||
        (lead == 0xed && static_cast<unsigned char>(value[i + 1]) >= 0xa0) ||
        (lead == 0xf0 && static_cast<unsigned char>(value[i + 1]) < 0x90) ||
        (lead == 0xf4 && static_cast<unsigned char>(value[i + 1]) >= 0x90))
      return false;
    i += count;
  }
  return true;
}

std::string normalize_text_encoding(const std::string &content) {
  if (is_valid_utf8(content)) return content;
#ifdef _WIN32
  const int wideLength = MultiByteToWideChar(
      CP_ACP, 0, content.data(), static_cast<int>(content.size()), nullptr, 0);
  if (wideLength > 0) {
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_ACP, 0, content.data(),
                        static_cast<int>(content.size()), wide.data(), wideLength);
    const int utf8Length = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (utf8Length > 0) {
      std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(),
                          utf8Length, nullptr, nullptr);
      return utf8;
    }
  }
#endif
  std::string safe;
  safe.reserve(content.size());
  for (const unsigned char ch : content) {
    if (ch <= 0x7f) safe.push_back(static_cast<char>(ch));
    else safe += "\xef\xbf\xbd";
  }
  return safe;
}

std::string read_file_bytes(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return "";
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string &value) {
  const int length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), length);
  return result;
}

bool reveal_file(const std::string &path) {
  const std::wstring widePath = utf8_to_wide(path);
  if (widePath.empty()) return false;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  PIDLIST_ABSOLUTE filePidl = nullptr;
  HRESULT result = SHParseDisplayName(widePath.c_str(), nullptr, &filePidl, 0, nullptr);
  if (SUCCEEDED(result) && filePidl) {
    PIDLIST_ABSOLUTE folderPidl = ILCloneFull(filePidl);
    if (folderPidl && ILRemoveLastID(folderPidl)) {
      PCUITEMID_CHILD child = ILFindLastID(filePidl);
      result = SHOpenFolderAndSelectItems(folderPidl, 1, &child, 0);
    } else {
      result = E_FAIL;
    }
    if (folderPidl) ILFree(folderPidl);
    CoTaskMemFree(filePidl);
  }
  if (uninitialize) CoUninitialize();
  return SUCCEEDED(result);
}
#endif

bool encode_for_save(const std::string &content, const std::string &encoding,
                     std::string &encoded) {
  if (!is_valid_utf8(content)) return false;
  if (encoding == "utf-8") {
    encoded = content;
    return true;
  }
  if (encoding == "utf-8-bom") {
    encoded = "\xef\xbb\xbf" + content;
    return true;
  }
  if (encoding != "system") return false;
#ifdef _WIN32
  const std::wstring wide = utf8_to_wide(content);
  if (content.size() > 0 && wide.empty()) return false;
  BOOL usedDefault = FALSE;
  const int length = WideCharToMultiByte(
      CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()),
      nullptr, 0, nullptr, &usedDefault);
  if (length < 0 || usedDefault) return false;
  encoded.assign(static_cast<std::size_t>(length), '\0');
  if (length > 0) {
    usedDefault = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(),
                            static_cast<int>(wide.size()), encoded.data(),
                            length, nullptr, &usedDefault) <= 0 || usedDefault) {
      return false;
    }
  }
  return true;
#else
  (void)content;
  (void)encoded;
  return false;
#endif
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
    if (is_known_binary_path(relative_path) ||
        workspace.isBinaryFile(relative_path)) {
      sqlite3_close(db);
      return error_response("BINARY_FILE", "binary files cannot be read",
                            "415 Unsupported Media Type");
    }

    std::string raw = read_file_bytes(full_path);
    std::string encoding = "utf-8";
    if (raw.size() >= 3 && raw.compare(0, 3, "\xef\xbb\xbf") == 0) {
      encoding = "utf-8-bom";
      raw.erase(0, 3);
    } else if (!is_valid_utf8(raw)) {
      encoding = "system";
    }
    const std::string content = normalize_text_encoding(raw);
    json body;
    body["success"] = true;
    body["data"] = {{"path", relative_path},
                    {"name", filename_from_path(relative_path)},
                    {"language", language_from_path(relative_path)},
                    {"content", content},
                    {"size", size},
                    {"readonly", false},
                    {"encoding", encoding}};
    sqlite3_close(db);
    return http_response(body.dump());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return error_response("DATABASE_ERROR", error.what(),
                          "500 Internal Server Error");
  }
}

std::string
WorkspaceFileController::saveFileContent(const std::string &request) {
  const std::string workspace_id = extract_workspace_id(request);
  const std::string relative_path = extract_query_string(request, "path");
  if (workspace_id.empty() || relative_path.empty()) {
    return error_response("INVALID_REQUEST",
                          "workspace_id and path are required");
  }

  json payload;
  try {
    payload = json::parse(request_body(request));
  } catch (const std::exception &) {
    return error_response("INVALID_REQUEST", "request body must be valid JSON");
  }
  if (!payload.contains("content") || !payload["content"].is_string()) {
    return error_response("INVALID_REQUEST", "content must be a string");
  }
  const std::string content = payload["content"].get<std::string>();
  const std::string encoding = payload.value("encoding", "utf-8");
  if (content.size() > static_cast<std::size_t>(kMaxReadableFileSize)) {
    return error_response("FILE_TOO_LARGE", "file exceeds maximum writable size",
                          "413 Payload Too Large");
  }

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) sqlite3_close(db);
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
      return error_response("FILE_NOT_FOUND", "file not found", "404 Not Found");
    }
    if (is_known_binary_path(relative_path) ||
        workspace.isBinaryFile(relative_path)) {
      sqlite3_close(db);
      return error_response("BINARY_FILE", "binary files cannot be written",
                            "415 Unsupported Media Type");
    }

    std::string encoded;
    if (!encode_for_save(content, encoding, encoded)) {
      sqlite3_close(db);
      return error_response("ENCODING_ERROR",
                            "content cannot be represented in the original encoding");
    }
    if (encoded.size() > static_cast<std::size_t>(kMaxReadableFileSize)) {
      sqlite3_close(db);
      return error_response("FILE_TOO_LARGE", "file exceeds maximum writable size",
                            "413 Payload Too Large");
    }
    std::ofstream output(full_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      sqlite3_close(db);
      return error_response("WRITE_FAILED", "file could not be opened for writing",
                            "500 Internal Server Error");
    }
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output) {
      sqlite3_close(db);
      return error_response("WRITE_FAILED", "file could not be written",
                            "500 Internal Server Error");
    }

    json body;
    body["success"] = true;
    body["data"] = {{"path", relative_path},
                    {"name", filename_from_path(relative_path)},
                    {"language", language_from_path(relative_path)},
                    {"content", content},
                    {"size", encoded.size()},
                    {"readonly", false},
                    {"encoding", encoding}};
    sqlite3_close(db);
    return http_response(body.dump());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return error_response("WRITE_FAILED", error.what(),
                          "500 Internal Server Error");
  }
}

std::string
WorkspaceFileController::revealInFileManager(const std::string &request) {
#ifndef _WIN32
  (void)request;
  return error_response("UNSUPPORTED_PLATFORM",
                        "file location reveal is only supported on Windows",
                        "501 Not Implemented");
#else
  const std::string workspace_id = extract_workspace_id(request);
  const std::string relative_path = extract_query_string(request, "path");
  if (workspace_id.empty() || relative_path.empty())
    return error_response("INVALID_REQUEST",
                          "workspace_id and path are required");

  sqlite3 *db = nullptr;
  if (openSqliteConnection(databasePath_.c_str(), &db) != SQLITE_OK) {
    const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
    if (db) sqlite3_close(db);
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
      return error_response("INVALID_PATH", "path is not allowed",
                            "403 Forbidden");
    }
    const std::string full_path = workspace.resolvePath(relative_path);
    if (!std::filesystem::exists(full_path) ||
        !std::filesystem::is_regular_file(full_path)) {
      sqlite3_close(db);
      return error_response("FILE_NOT_FOUND", "file not found",
                            "404 Not Found");
    }
    const bool revealed = reveal_file(full_path);
    sqlite3_close(db);
    if (!revealed)
      return error_response("REVEAL_FAILED",
                            "Windows Explorer could not locate the file",
                            "500 Internal Server Error");
    json body;
    body["success"] = true;
    body["data"] = {{"revealed", true}, {"path", relative_path}};
    return http_response(body.dump());
  } catch (const std::exception &error) {
    sqlite3_close(db);
    return error_response("REVEAL_FAILED", error.what(),
                          "500 Internal Server Error");
  }
#endif
}

} // namespace codepilot

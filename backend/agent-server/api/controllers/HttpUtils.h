#pragma once

#include <chrono>
#include <ctime>
#include <sstream>
#include <string>

namespace codepilot {

// ============================================================
// 公共 HTTP 工具函数（消除 SessionController / WorkspaceController
// / GlobalController 之间的重复代码）
// ============================================================

inline std::string json_escape(const std::string &value) {
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

inline std::string http_response(const std::string &body,
                                 const std::string &status = "200 OK") {
  std::ostringstream response;
  response
      << "HTTP/1.1 " << status << "\r\n"
      << "Content-Type: application/json; charset=utf-8\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Connection: close\r\n"
      << "Content-Length: " << body.size() << "\r\n\r\n"
      << body;
  return response.str();
}

inline std::string current_timestamp() {
  const auto now = std::time(nullptr);
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return buf;
}

inline std::string generate_id(const std::string &prefix) {
  return prefix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

inline std::string request_body(const std::string &request) {
  const std::size_t body_pos = request.find("\r\n\r\n");
  if (body_pos == std::string::npos)
    return "";
  return request.substr(body_pos + 4);
}

inline std::string extract_path_segment(const std::string &request,
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

inline std::string extract_json_string(const std::string &body,
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

} // namespace codepilot
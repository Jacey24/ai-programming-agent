// util.hpp — Shared utilities for the Astral system
#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace astral {
namespace util {

// JSON string escaping (for embedding strings into JSON payloads)
inline std::string json_escape(const std::string &s) {
  std::string r;
  r.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':
      r += "\\\"";
      break;
    case '\\':
      r += "\\\\";
      break;
    case '\n':
      r += "\\n";
      break;
    case '\t':
      r += "\\t";
      break;
    case '\r':
      r += "\\r";
      break;
    default:
      r += c;
      break;
    }
  }
  return r;
}

// Decode JSON escape sequences (\\n → newline, \\\" → ", etc.)
inline std::string json_unescape(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
      case 'n':
        r += '\n';
        i++;
        break;
      case 't':
        r += '\t';
        i++;
        break;
      case 'r':
        r += '\r';
        i++;
        break;
      case '"':
        r += '"';
        i++;
        break;
      case '\\':
        r += '\\';
        i++;
        break;
      default:
        r += s[i];
        break;
      }
    } else {
      r += s[i];
    }
  }
  return r;
}

// Uppercase a string
inline std::string to_upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<unsigned char>(std::toupper(c));
  });
  return s;
}

} // namespace util
} // namespace astral
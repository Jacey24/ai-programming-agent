// skill_txt.exe — Standalone TXT file editor tool
// Build: cl /EHsc /std:c++20 /utf-8 skill_txt_main.cpp
// /Fe:../skills/skill_txt.exe Protocol: first arg = command, rest = arguments
// Output: JSON { "ok": true/false, "msg": "...", "data": {...} }

// NOTE: Multi-line content should use \n (escaping) inside the command line.
// The exe will convert literal \n sequences to actual newlines.
// Literal backslash should be written as \\.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ========== JSON escape ==========
static std::string json_esc(const std::string &s) {
  std::string r;
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

static void out_json(bool ok, const std::string &msg,
                     const std::string &data = "{}") {
  std::string result = "{\"ok\":" + std::string(ok ? "true" : "false") +
                       ",\"msg\":\"" + json_esc(msg) + "\",\"data\":" + data +
                       "}\n";
  std::cout << result << std::flush;
}

// ========== Utility ==========

// Convert escape sequences in command-line content:
//   \n -> actual newline
//   \\ -> literal backslash
//   \t -> actual tab
static std::string unescape_content(const std::string &s) {
  std::string r;
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

// Read entire file into string
static std::string read_file_content(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
    return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Write string to file
static bool write_file_content(const std::string &path,
                               const std::string &content) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open())
    return false;
  out.write(content.data(), (long long)content.size());
  return out.good();
}

// Split string by delimiter
static std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);
  return parts;
}

// Count actual lines in content (after unescaping)
static int count_lines(const std::string &content) {
  if (content.empty())
    return 0;
  int count = 0;
  for (char c : content) {
    if (c == '\n')
      count++;
  }
  if (content.back() != '\n')
    count++;
  return count;
}

// ========== Allowed extensions ==========
static bool is_allowed_ext(const std::string &path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".txt" || ext == ".bat" || ext == ".cmd" || ext == ".json";
}

// If path has no extension, append the extension corresponding to type_flag
// type_flag: 'b' -> .bat, 'j' -> .json, 't' -> .txt, 0 -> no change
static std::string apply_type_to_path(const std::string &path, char type_flag) {
  if (type_flag == 0)
    return path;
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (!ext.empty())
    return path; // already has extension, keep it
  switch (type_flag) {
  case 'b':
    return path + ".bat";
  case 'j':
    return path + ".json";
  case 't':
    return path + ".txt";
  default:
    return path;
  }
}

// ========== Command Implementations ==========

static void cmd_read(const std::string &path) {
  if (!fs::exists(path)) {
    out_json(false, "文件不存在: " + path);
    return;
  }

  if (fs::is_directory(path)) {
    out_json(false, "路径是目录，不是文件: " + path);
    return;
  }

  // Check extension
  if (!is_allowed_ext(path)) {
    std::string ext = fs::path(path).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，当前文件扩展名: " + ext);
    return;
  }

  std::string content = read_file_content(path);
  int total_lines = count_lines(content);

  // Build data JSON with content
  std::string data = "{\"path\":\"" + json_esc(path) +
                     "\",\"total_lines\":" + std::to_string(total_lines) +
                     ",\"content\":\"" + json_esc(content) + "\"}";

  std::string msg = "文件读取成功，共" + std::to_string(total_lines) + "行";
  out_json(true, msg, data);
}

static void cmd_append(const std::string &path, const std::string &content) {
  if (!fs::exists(path)) {
    out_json(false, "文件不存在: " + path);
    return;
  }

  if (fs::is_directory(path)) {
    out_json(false, "路径是目录，不是文件: " + path);
    return;
  }

  if (!is_allowed_ext(path)) {
    std::string ext = fs::path(path).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，当前文件扩展名: " + ext);
    return;
  }

  // Unescape content before appending
  std::string final_content = unescape_content(content);

  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out.is_open()) {
    out_json(false, "无法打开文件进行追加: " + path);
    return;
  }

  out << "\n" << final_content;
  out.close();

  std::string data =
      "{\"path\":\"" + json_esc(path) + "\",\"appended_lines\":1}";
  std::string msg = "已成功追加内容到: " + path;
  out_json(true, msg, data);
}

static void cmd_write(const std::string &path, const std::string &content) {
  if (!is_allowed_ext(path)) {
    std::string ext = fs::path(path).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，当前文件扩展名: " + ext);
    return;
  }

  // Ensure parent directory exists
  fs::path parent = fs::path(path).parent_path();
  if (!parent.empty() && !fs::exists(parent)) {
    fs::create_directories(parent);
  }

  // Unescape content before writing
  std::string final_content = unescape_content(content);

  if (!write_file_content(path, final_content)) {
    out_json(false, "无法写入文件: " + path);
    return;
  }

  int total_lines = count_lines(final_content);
  std::string data = "{\"path\":\"" + json_esc(path) +
                     "\",\"total_lines\":" + std::to_string(total_lines) + "}";
  std::string msg = "文件写入成功，共" + std::to_string(total_lines) + "行";
  out_json(true, msg, data);
}

static void cmd_delete(const std::string &path) {
  if (!fs::exists(path)) {
    out_json(false, "文件不存在: " + path);
    return;
  }

  if (fs::is_directory(path)) {
    out_json(false, "路径是目录，不是文件: " + path);
    return;
  }

  if (!is_allowed_ext(path)) {
    std::string ext = fs::path(path).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，当前文件扩展名: " + ext);
    return;
  }

  if (!fs::remove(path)) {
    out_json(false, "删除文件失败: " + path);
    return;
  }

  std::string data = "{\"path\":\"" + json_esc(path) + "\"}";
  std::string msg = "已成功删除文件: " + path;
  out_json(true, msg, data);
}

static void cmd_rename(const std::string &src, const std::string &dst) {
  if (!fs::exists(src)) {
    out_json(false, "源文件不存在: " + src);
    return;
  }

  if (fs::is_directory(src)) {
    out_json(false, "源路径是目录，不是文件: " + src);
    return;
  }

  if (!is_allowed_ext(src)) {
    std::string ext = fs::path(src).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，源文件扩展名: " + ext);
    return;
  }

  // Ensure parent directory of destination exists
  fs::path parent = fs::path(dst).parent_path();
  if (!parent.empty() && !fs::exists(parent)) {
    fs::create_directories(parent);
  }

  if (fs::exists(dst)) {
    out_json(false, "目标文件已存在: " + dst);
    return;
  }

  std::error_code ec;
  fs::rename(src, dst, ec);
  if (ec) {
    out_json(false, "重命名文件失败: " + ec.message());
    return;
  }

  std::string data =
      "{\"src\":\"" + json_esc(src) + "\",\"dst\":\"" + json_esc(dst) + "\"}";
  std::string msg = "已成功将文件重命名为: " + dst;
  out_json(true, msg, data);
}

static void cmd_line(const std::string &path, int line_num,
                     const std::string &new_content) {
  if (!fs::exists(path)) {
    out_json(false, "文件不存在: " + path);
    return;
  }

  if (fs::is_directory(path)) {
    out_json(false, "路径是目录，不是文件: " + path);
    return;
  }

  if (!is_allowed_ext(path)) {
    std::string ext = fs::path(path).extension().string();
    out_json(false, "只能操作.txt/.bat/.json文件，当前文件扩展名: " + ext);
    return;
  }

  if (line_num < 1) {
    out_json(false, "行号必须大于等于1");
    return;
  }

  std::string content = read_file_content(path);
  std::vector<std::string> lines;
  std::string cur;
  for (char c : content) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty() || (!content.empty() && content.back() == '\n')) {
    lines.push_back(cur);
  }

  if (line_num > (int)lines.size()) {
    out_json(false, "行号超出范围。文件共" + std::to_string(lines.size()) +
                        "行，目标行号: " + std::to_string(line_num));
    return;
  }

  // Replace the line
  std::string old_line = lines[line_num - 1];
  lines[line_num - 1] = new_content;

  std::string new_file_content;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i > 0)
      new_file_content += "\n";
    new_file_content += lines[i];
  }

  if (!write_file_content(path, new_file_content)) {
    out_json(false, "无法写入文件: " + path);
    return;
  }

  std::string data = "{\"path\":\"" + json_esc(path) +
                     "\",\"line\":" + std::to_string(line_num) +
                     ",\"old_content\":\"" + json_esc(old_line) +
                     "\",\"new_content\":\"" + json_esc(new_content) + "\"}";
  std::string msg = "已替换文件第" + std::to_string(line_num) + "行内容";
  out_json(true, msg, data);
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif

  if (argc < 3) {
    out_json(
        false,
        "用法: skill_txt.exe <READ|APPEND|WRITE|LINE|DELETE|RENAME> <参数...>");
    return 1;
  }

  // Parse command
  std::string cmd = argv[1];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd == "READ") {
    // READ <filepath>  — filepath is argv[2..]
    std::string filepath = argv[2];
    for (int i = 3; i < argc; i++) {
      filepath += " " + std::string(argv[i]);
    }
    cmd_read(filepath);
  } else if (cmd == "APPEND") {
    // APPEND <filepath> | <content>
    std::string full_args;
    for (int i = 2; i < argc; i++) {
      if (i > 2)
        full_args += " ";
      full_args += argv[i];
    }
    auto parts = split(full_args, '|');
    if (parts.size() < 2) {
      out_json(false, "APPEND 用法: APPEND <文件路径> | <内容>");
      return 1;
    }
    std::string filepath = parts[0];
    while (!filepath.empty() && filepath.back() == ' ')
      filepath.pop_back();

    std::string content = parts[1];
    for (size_t i = 2; i < parts.size(); i++) {
      content += "|" + parts[i];
    }
    // Trim leading space from content (the space after | )
    if (!content.empty() && content.front() == ' ')
      content.erase(0, 1);

    cmd_append(filepath, content);
  } else if (cmd == "WRITE") {
    // WRITE <文件路径> | <内容>
    // 或 WRITE -b <文件路径> | <内容>   (自动加.bat)
    // 或 WRITE -j <文件路径> | <内容>   (自动加.json)
    // 或 WRITE -t <文件路径> | <内容>   (自动加.txt)
    std::string full_args;
    char type_flag = 0;
    // Check for type flag in argv[2]
    if (argc > 3 && std::string(argv[2]).size() == 2 && argv[2][0] == '-') {
      char flag = std::tolower(argv[2][1]);
      if (flag == 'b' || flag == 'j' || flag == 't') {
        type_flag = flag;
        for (int i = 3; i < argc; i++) {
          if (i > 3)
            full_args += " ";
          full_args += argv[i];
        }
      } else {
        for (int i = 2; i < argc; i++) {
          if (i > 2)
            full_args += " ";
          full_args += argv[i];
        }
      }
    } else {
      for (int i = 2; i < argc; i++) {
        if (i > 2)
          full_args += " ";
        full_args += argv[i];
      }
    }
    auto parts = split(full_args, '|');
    if (parts.size() < 2) {
      out_json(false, "WRITE 用法: WRITE [-b|-j|-t] <文件路径> | <内容>");
      return 1;
    }
    std::string filepath = parts[0];
    while (!filepath.empty() && filepath.back() == ' ')
      filepath.pop_back();
    filepath = apply_type_to_path(filepath, type_flag);

    std::string content = parts[1];
    for (size_t i = 2; i < parts.size(); i++) {
      content += "|" + parts[i];
    }
    // Trim leading space from content
    if (!content.empty() && content.front() == ' ')
      content.erase(0, 1);

    cmd_write(filepath, content);
  } else if (cmd == "LINE") {
    // LINE <filepath> | <line_num> | <new_content>
    std::string full_args;
    for (int i = 2; i < argc; i++) {
      if (i > 2)
        full_args += " ";
      full_args += argv[i];
    }
    auto parts = split(full_args, '|');
    if (parts.size() < 3) {
      out_json(false, "LINE 用法: LINE <文件路径> | <行号> | <新内容>");
      return 1;
    }
    std::string filepath = parts[0];
    while (!filepath.empty() && filepath.back() == ' ')
      filepath.pop_back();

    std::string line_str = parts[1];
    while (!line_str.empty() && line_str.front() == ' ')
      line_str.erase(0, 1);
    while (!line_str.empty() && line_str.back() == ' ')
      line_str.pop_back();

    int line_num = std::stoi(line_str);
    std::string new_content = parts[2];
    for (size_t i = 3; i < parts.size(); i++) {
      new_content += "|" + parts[i];
    }

    cmd_line(filepath, line_num, new_content);
  } else {
    out_json(false,
             "未知命令: " + cmd +
                 "。可用命令: READ, APPEND, WRITE, LINE, DELETE, RENAME");
    return 1;
  }

  return 0;
}
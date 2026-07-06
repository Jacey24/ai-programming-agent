// skill_sys.exe — System info & directory listing tool
// Build: cl /EHsc /std:c++20 /utf-8 skill_sys_main.cpp
// /Fe:../skills/skill_sys.exe Protocol: first arg = command, rest = arguments
// Output: JSON { "ok": true/false, "msg": "...", "data": {...} }
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "secur32.lib")
#pragma warning(push)
#pragma warning(disable : 4005)
#include <lmcons.h>
#include <windows.h>
#pragma warning(pop)
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

// ========== Command Implementations ==========

static void cmd_whoami() {
#ifdef _WIN32
  char username[UNLEN + 1];
  DWORD len = UNLEN + 1;
  if (GetUserNameA(username, &len)) {
    char compname[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD clen = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(compname, &clen)) {
      std::string data = "{\"username\":\"" + json_esc(username) +
                         "\",\"computer\":\"" + json_esc(compname) + "\"}";
      std::string msg = std::string(username) + "@" + std::string(compname);
      out_json(true, msg, data);
      return;
    }
    out_json(true, username);
    return;
  }
  out_json(false, "无法获取用户名");
#else
  const char *user = std::getenv("USER");
  if (!user)
    user = std::getenv("LOGNAME");
  if (user) {
    out_json(true, user);
  } else {
    out_json(false, "无法获取用户名");
  }
#endif
}

static void cmd_datetime() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  char weekday_buf[16];
  std::strftime(weekday_buf, sizeof(weekday_buf), "%A", &tm);
  char tz_buf[16];
  std::strftime(tz_buf, sizeof(tz_buf), "%z", &tm);

  std::string data = "{\"datetime\":\"" + json_esc(buf) + "\",\"weekday\":\"" +
                     json_esc(weekday_buf) + "\",\"timezone\":\"" +
                     json_esc(tz_buf) + "\"}";
  std::string msg = std::string(buf) + " (" + weekday_buf + ")";
  out_json(true, msg, data);
}

static void cmd_dir(const std::string &path) {
  fs::path target_path = path;
  if (path.empty() || path == "." || path == "..")
    target_path = fs::current_path();

  if (!fs::exists(target_path)) {
    out_json(false, "路径不存在: " + path);
    return;
  }
  if (!fs::is_directory(target_path)) {
    out_json(false, "路径不是目录: " + path);
    return;
  }

  std::string entries_json;
  int file_count = 0, dir_count = 0;
  bool first = true;

  try {
    for (auto &entry : fs::directory_iterator(target_path)) {
      if (!first)
        entries_json += ",";
      first = false;

      std::string name = entry.path().filename().string();
      std::string type = entry.is_directory() ? "dir" : "file";
      uintmax_t size = entry.is_regular_file() ? fs::file_size(entry) : 0;

      entries_json += "{\"name\":\"" + json_esc(name) + "\",\"type\":\"" +
                      type + "\",\"size\":" + std::to_string((long long)size) +
                      "}";

      if (entry.is_directory())
        dir_count++;
      else
        file_count++;
    }
  } catch (const std::exception &e) {
    out_json(false, "读取目录失败: " + std::string(e.what()));
    return;
  }

  std::string abs_path = fs::absolute(target_path).string();
  std::string data = "{\"path\":\"" + json_esc(abs_path) +
                     "\",\"directories\":" + std::to_string(dir_count) +
                     ",\"files\":" + std::to_string(file_count) +
                     ",\"entries\":[" + entries_json + "]}";
  std::string msg = abs_path + " — " + std::to_string(dir_count) + "个目录, " +
                    std::to_string(file_count) + "个文件";
  out_json(true, msg, data);
}

static void cmd_pwd() {
  std::string cwd = fs::current_path().string();
  std::string data = "{\"path\":\"" + json_esc(cwd) + "\"}";
  out_json(true, cwd, data);
}

// Execute a PowerShell command and return its output as JSON
static void cmd_ps(const std::string &command) {
  if (command.empty()) {
    out_json(false, "请提供 PowerShell 命令");
    return;
  }
#ifdef _WIN32
  // Build the PowerShell command line
  // Use powershell -Command with proper escaping
  std::string ps_cmd = "powershell -NoProfile -Command \"" + command + "\"";

  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

  HANDLE hStdoutRd, hStdoutWr;
  if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0)) {
    out_json(false, "创建管道失败");
    return;
  }
  SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

  HANDLE hStderrRd, hStderrWr;
  if (!CreatePipe(&hStderrRd, &hStderrWr, &sa, 0)) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    out_json(false, "创建管道失败");
    return;
  }
  SetHandleInformation(hStderrRd, HANDLE_FLAG_INHERIT, 0);

  PROCESS_INFORMATION pi = {0};
  STARTUPINFOW si = {sizeof(STARTUPINFOW)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hStdoutWr;
  si.hStdError = hStderrWr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  // Convert to UTF-16 for CreateProcessW (preserves Unicode chars)
  std::wstring cmdline_w;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, ps_cmd.c_str(), -1, nullptr, 0);
  if (wlen > 0) {
    cmdline_w.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, ps_cmd.c_str(), -1, &cmdline_w[0], wlen);
    if (!cmdline_w.empty() && cmdline_w.back() == L'\0')
      cmdline_w.pop_back();
  } else {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    CloseHandle(hStderrRd);
    CloseHandle(hStderrWr);
    out_json(false, "命令编码转换失败");
    return;
  }

  bool created = CreateProcessW(nullptr, &cmdline_w[0], nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

  CloseHandle(hStdoutWr);
  CloseHandle(hStderrWr);

  if (!created) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStderrRd);
    out_json(false, "PowerShell 启动失败");
    return;
  }

  // Wait up to 30 seconds
  WaitForSingleObject(pi.hProcess, 30000);

  // Read stdout
  std::string stdout_text;
  char buf[4096];
  DWORD read;
  while (ReadFile(hStdoutRd, buf, sizeof(buf) - 1, &read, nullptr) &&
         read > 0) {
    buf[read] = '\0';
    stdout_text += buf;
  }

  // Read stderr
  std::string stderr_text;
  while (ReadFile(hStderrRd, buf, sizeof(buf) - 1, &read, nullptr) &&
         read > 0) {
    buf[read] = '\0';
    stderr_text += buf;
  }

  // Get exit code
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(hStdoutRd);
  CloseHandle(hStderrRd);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  // Trim trailing newlines
  while (!stdout_text.empty() &&
         (stdout_text.back() == '\n' || stdout_text.back() == '\r'))
    stdout_text.pop_back();

  std::string data = "{\"exit_code\":" + std::to_string((int)exit_code) +
                     ",\"stdout\":\"" + json_esc(stdout_text) + "\"";
  if (!stderr_text.empty())
    data += ",\"stderr\":\"" + json_esc(stderr_text) + "\"";
  data += "}";

  std::string msg =
      "PS 执行完毕 (退出码: " + std::to_string((int)exit_code) + ")";
  // Truncate stdout in msg if too long
  std::string preview = stdout_text.substr(0, 200);
  if (stdout_text.size() > 200)
    preview += "...";
  msg += "\n" + preview;

  out_json(true, msg, data);
#else
  // Non-Windows fallback: use popen with sh
  std::string cmdline = command + " 2>&1";
  FILE *fp = popen(cmdline.c_str(), "r");
  if (!fp) {
    out_json(false, "无法执行命令");
    return;
  }
  std::string output;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp))
    output += buf;
  int rc = pclose(fp);
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    output.pop_back();
  std::string data = "{\"exit_code\":" + std::to_string(rc) + ",\"stdout\":\"" +
                     json_esc(output) + "\"}";
  out_json(true, "PS 执行完毕 (退出码: " + std::to_string(rc) + ")", data);
#endif
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  if (argc < 2) {
    out_json(false, "用法: skill_sys.exe <WHOAMI|DATETIME|DIR|PWD> [参数]");
    return 1;
  }

  std::string cmd = argv[1];
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

  if (cmd == "WHOAMI")
    cmd_whoami();
  else if (cmd == "DATETIME")
    cmd_datetime();
  else if (cmd == "DIR") {
    std::string path;
    if (argc >= 3) {
      path = argv[2];
      for (int i = 3; i < argc; i++)
        path += " " + std::string(argv[i]);
    }
    cmd_dir(path);
  } else if (cmd == "PWD")
    cmd_pwd();
  else if (cmd == "PS") {
    std::string ps_cmd;
    for (int i = 2; i < argc; i++) {
      if (i > 2)
        ps_cmd += " ";
      ps_cmd += argv[i];
    }
    cmd_ps(ps_cmd);
  } else {
    out_json(false,
             "未知命令: " + cmd + "。可用命令: WHOAMI, DATETIME, DIR, PWD, PS");
    return 1;
  }
  return 0;
}
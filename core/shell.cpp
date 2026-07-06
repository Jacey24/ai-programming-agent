// shell.cpp — Unified command routing for both AI skill commands and CLI
#include "shell.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astral {

#ifdef _WIN32
// Convert UTF-8 string to UTF-16 wstring (for CreateProcessW)
static std::wstring utf8_to_utf16(const std::string &utf8) {
  if (utf8.empty())
    return L"";
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (wlen <= 0)
    return L"";
  std::wstring wstr(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
  if (!wstr.empty() && wstr.back() == L'\0')
    wstr.pop_back();
  return wstr;
}
#endif

void Shell::register_cmd(const std::string &cmd, const std::string &exe_path,
                         bool dangerous, const std::string &description) {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  routes_[upper] = {exe_path, dangerous, description, nullptr};
}

void Shell::register_builtin(
    const std::string &cmd,
    std::function<bool(const std::string &args, ShellResult &out)> handler,
    bool dangerous, const std::string &description) {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  routes_[upper] = {"", dangerous, description, handler};
}

bool Shell::has_cmd(const std::string &cmd) const {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  return routes_.count(upper);
}

bool Shell::is_dangerous(const std::string &cmd) const {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  auto it = routes_.find(upper);
  return it != routes_.end() && it->second.dangerous;
}

std::string Shell::cmd_description(const std::string &cmd) const {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  auto it = routes_.find(upper);
  if (it != routes_.end())
    return it->second.description;
  return "";
}

std::vector<std::string> Shell::list_commands() const {
  std::vector<std::string> cmds;
  for (auto &[k, _] : routes_)
    cmds.push_back(k);
  return cmds;
}

std::vector<std::pair<std::string, bool>>
Shell::list_commands_with_dangerous() const {
  std::vector<std::pair<std::string, bool>> result;
  for (auto &[k, v] : routes_)
    result.push_back({k, v.dangerous});
  return result;
}

std::string Shell::help_text() const {
  std::string s = "=== Astral 命令列表 ===\n\n";

  // Separate builtin CLI commands from skill commands
  std::vector<std::pair<std::string, CmdRoute>> builtins, skills;
  for (auto &[k, v] : routes_) {
    if (v.exe_path.empty())
      builtins.push_back({k, v});
    else
      skills.push_back({k, v});
  }

  if (!builtins.empty()) {
    s += "【系统命令】\n";
    for (auto &[name, route] : builtins) {
      std::string d =
          route.description.empty() ? "(无说明)" : route.description;
      s += "  /" + name + (d.empty() ? "" : " — " + d) + "\n";
    }
    s += "\n";
  }

  if (!skills.empty()) {
    s += "【技能指令】\n";
    for (auto &[name, route] : skills) {
      std::string d =
          route.description.empty() ? "(无说明)" : route.description;
      s += "  " + name + (route.dangerous ? " ⚠️" : "") +
           (d.empty() ? "" : " — " + d) + "\n";
    }
    s += "\n提示: 技能指令可由AI调用，也可手动输入 /指令名 参数 执行\n";
  }
  return s;
}

#ifdef _WIN32
// Run a subprocess and capture its stdout (Unicode-safe with CreateProcessW)
static ShellResult capture_process(const std::string &full_cmd_line,
                                   int timeout_sec) {
  ShellResult r = {false, -1, "", ""};

  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

  HANDLE hStdoutRd, hStdoutWr;
  if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0))
    return r;
  if (!SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    return r;
  }

  HANDLE hStderrRd, hStderrWr;
  if (!CreatePipe(&hStderrRd, &hStderrWr, &sa, 0)) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    return r;
  }
  if (!SetHandleInformation(hStderrRd, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    CloseHandle(hStderrRd);
    CloseHandle(hStderrWr);
    return r;
  }

  PROCESS_INFORMATION pi = {0};
  STARTUPINFOW si = {sizeof(STARTUPINFOW)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hStdoutWr;
  si.hStdError = hStderrWr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  std::wstring cmdline_w = utf8_to_utf16(full_cmd_line);

  if (!CreateProcessW(nullptr, &cmdline_w[0], nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(hStdoutRd);
    CloseHandle(hStdoutWr);
    CloseHandle(hStderrRd);
    CloseHandle(hStderrWr);
    r.stderr_text = "CreateProcessW failed";
    return r;
  }

  CloseHandle(hStdoutWr);
  CloseHandle(hStderrWr);

  // Read stdout/stderr in separate threads WHILE process is running
  // to prevent pipe deadlock with large output.
  std::string stdout_text, stderr_text;
  std::atomic<bool> stdout_done(false), stderr_done(false);

  std::thread stdout_reader([&]() {
    char buf[65536];
    DWORD read;
    while (ReadFile(hStdoutRd, buf, sizeof(buf) - 1, &read, nullptr) &&
           read > 0) {
      buf[read] = '\0';
      stdout_text += buf;
    }
    stdout_done = true;
  });

  std::thread stderr_reader([&]() {
    char buf[4096];
    DWORD read;
    while (ReadFile(hStderrRd, buf, sizeof(buf) - 1, &read, nullptr) &&
           read > 0) {
      buf[read] = '\0';
      stderr_text += buf;
    }
    stderr_done = true;
  });

  // Wait for process with timeout
  DWORD wait = WaitForSingleObject(pi.hProcess, timeout_sec * 1000);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    r.stderr_text = "Timeout (" + std::to_string(timeout_sec) + "s)";
  } else {
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    r.exit_code = (int)exit_code;
  }

  // Wait briefly for reader threads to finish
  auto join_start = std::chrono::steady_clock::now();
  while (!stdout_done || !stderr_done) {
    auto elapsed = std::chrono::steady_clock::now() - join_start;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >
        2000)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (stdout_reader.joinable())
    stdout_reader.join();
  if (stderr_reader.joinable())
    stderr_reader.join();

  CloseHandle(hStdoutRd);
  CloseHandle(hStderrRd);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  r.stdout_text = stdout_text;
  if (r.stderr_text.empty())
    r.stderr_text = stderr_text;
  r.ok = (r.exit_code == 0);
  return r;
}
#endif

ShellResult Shell::run(const std::string &cmd_line, int timeout_sec) {
  // Parse: first word = command, rest = args
  std::istringstream iss(cmd_line);
  std::string cmd_name;
  iss >> cmd_name;
  if (cmd_name.empty())
    return {false, -1, "", "empty command"};

  // Uppercase for lookup
  std::string upper = cmd_name;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  // Find route
  auto it = routes_.find(upper);
  if (it == routes_.end()) {
    return {false, -1, "",
            "未知命令: " + cmd_name + "。输入 /help 查看可用命令。"};
  }

  auto &route = it->second;

  // Get the args part (everything after the command name)
  std::string args;
  std::getline(iss, args);
  // Trim leading space
  auto first = args.find_first_not_of(" \t");
  if (first != std::string::npos)
    args = args.substr(first);

  // === Builtin command: execute handler ===
  if (route.handler) {
    ShellResult result;
    bool handled = route.handler(args, result);
    if (handled)
      return result;
    // If handler returned false, fall through to default behavior
  }

  // === External skill command: run exe ===
  if (!route.exe_path.empty()) {
    std::string full_cmd = route.exe_path + " " + upper + " " + args;
    return capture_process(full_cmd, timeout_sec);
  }

  return {false, -1, "", "命令 " + upper + " 未实现。"};
}

ShellResult Shell::run_exe(const std::string &exe_path,
                           const std::vector<std::string> &args,
                           int timeout_sec) {
  std::string full_cmd = exe_path;
  for (auto &a : args) {
    full_cmd += " \"" + a + "\"";
  }
  return capture_process(full_cmd, timeout_sec);
}

} // namespace astral
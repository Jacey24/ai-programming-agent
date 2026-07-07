#include "ProcessRunner.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace codepilot {

ProcessRunner::ProcessRunner() : workingDirectory_("") {}

void ProcessRunner::setWorkingDirectory(const std::string &cwd) {
  workingDirectory_ = cwd;
}

std::string ProcessRunner::workingDirectory() const {
  return workingDirectory_;
}

// 在 Windows 上用 _popen 封装
ProcessResult ProcessRunner::execute(const std::string &command, int timeout) {
  ProcessResult result;

  // 构建带 cd 的命令
  std::string fullCmd = command;
  if (!workingDirectory_.empty()) {
    fullCmd = "cd /d \"" + workingDirectory_ + "\" && " + command;
  }

  // 重定向 stderr 到 stdout
  fullCmd += " 2>&1";

  // 使用 _popen 执行
  FILE *pipe = _popen(fullCmd.c_str(), "r");
  if (!pipe) {
    result.success = false;
    result.errorOutput = "Failed to start process";
    result.exitCode = -1;
    return result;
  }

  // 读取输出
  std::string output;
  char buf[4096];
  auto startTime = std::chrono::steady_clock::now();
  bool timedOut = false;

  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    output += buf;

    if (timeout > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - startTime)
                         .count();
      if (elapsed >= timeout) {
        timedOut = true;
        break;
      }
    }
  }

  int exitCode = _pclose(pipe);

  result.output = output;
  result.errorOutput =
      timedOut ? "Command timed out after " + std::to_string(timeout) + "s"
               : "";

  if (timedOut) {
    result.success = false;
    result.exitCode = -1;
  } else {
    result.exitCode = exitCode;
    result.success = (exitCode == 0);
  }

  return result;
}

} // namespace codepilot
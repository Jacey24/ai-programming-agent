#pragma once

#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// ProcessRunner — 进程执行器
//
// 在 Windows 上使用 _popen 执行命令行命令
// 对齐整体架构说明.md 第6.2节 infrastructure/process/
// ============================================================
struct ProcessResult {
  bool success{false};
  std::string output;
  std::string errorOutput;
  int exitCode{-1};
};

class ProcessRunner {
public:
  ProcessRunner();

  // --- 执行命令（同步阻塞）---
  // command: 要执行的命令
  // timeout: 超时秒数（0 = 不限）
  ProcessResult execute(const std::string &command, int timeout = 60);

  // --- 设置工作目录（可选）---
  void setWorkingDirectory(const std::string &cwd);

  // --- 获取工作目录 ---
  std::string workingDirectory() const;

private:
  std::string workingDirectory_;
};

} // namespace codepilot
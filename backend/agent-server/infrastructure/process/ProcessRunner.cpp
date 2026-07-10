#include "ProcessRunner.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
    fullCmd = "cd /d \"" + workingDirectory_ + "\" && " + command;
#else
    fullCmd = "cd \"" + workingDirectory_ + "\" && " + command;
#endif
  }

#ifdef _WIN32
  // Windows：沿用 _popen（读取阻塞，超时按行检查）
  fullCmd += " 2>&1";
  FILE *pipe = _popen(fullCmd.c_str(), "r");
  if (!pipe) {
    result.success = false;
    result.errorOutput = "Failed to start process";
    result.exitCode = -1;
    return result;
  }

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
  if (timedOut) {
    result.success = false;
    result.exitCode = -1;
    result.errorOutput =
        "Command timed out after " + std::to_string(timeout) + "s";
  } else {
    result.exitCode = exitCode;
    result.success = (exitCode == 0);
  }
  return result;
#else
  // POSIX：fork + exec + pipe + select 超时。
  // 关键：即使子命令没有任何输出且不退出（等待 stdin / 死循环），
  // 也能在超时后终止子进程组，而不是永久阻塞在读取上。
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    result.success = false;
    result.errorOutput = "Failed to create pipe";
    result.exitCode = -1;
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    result.success = false;
    result.errorOutput = "Failed to fork process";
    result.exitCode = -1;
    return result;
  }

  if (pid == 0) {
    // 子进程：stdout/stderr 重定向到管道，独立进程组以便整体终止。
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    setpgid(0, 0);
    execl("/bin/sh", "sh", "-c", fullCmd.c_str(), static_cast<char *>(nullptr));
    _exit(127); // exec 失败
  }

  // 父进程
  close(pipefd[1]);
  setpgid(pid, pid); // 与子进程竞争性地设置进程组，双方设置都安全

  std::string output;
  bool timedOut = false;
  const auto startTime = std::chrono::steady_clock::now();

  for (;;) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(pipefd[0], &readSet);

    struct timeval tv;
    struct timeval *tvPtr = nullptr;
    if (timeout > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - startTime)
                               .count();
      if (elapsed >= timeout) {
        timedOut = true;
        break;
      }
      tv.tv_sec = static_cast<long>(timeout - elapsed);
      tv.tv_usec = 0;
      tvPtr = &tv;
    }

    const int rv = select(pipefd[0] + 1, &readSet, nullptr, nullptr, tvPtr);
    if (rv == -1) {
      if (errno == EINTR) {
        continue;
      }
      break; // select 出错
    }
    if (rv == 0) {
      timedOut = true; // select 超时（无任何输出）
      break;
    }

    char buf[4096];
    const ssize_t n = read(pipefd[0], buf, sizeof(buf));
    if (n > 0) {
      output.append(buf, static_cast<std::size_t>(n));
    } else if (n == 0) {
      break; // EOF：子进程关闭了输出
    } else {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
  }

  close(pipefd[0]);

  if (timedOut) {
    // 终止整个子进程组，避免 waitpid 卡在挂起的子进程上。
    kill(-pid, SIGKILL);
  }

  int status = 0;
  waitpid(pid, &status, 0);

  result.output = output;
  if (timedOut) {
    result.success = false;
    result.exitCode = -1;
    result.errorOutput =
        "Command timed out after " + std::to_string(timeout) + "s";
  } else if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
    result.success = (result.exitCode == 0);
  } else {
    result.exitCode = -1;
    result.success = false;
    result.errorOutput = "Command terminated abnormally";
  }
  return result;
#endif
}

} // namespace codepilot
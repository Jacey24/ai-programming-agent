#include "RiskDetector.h"
#include <algorithm>
#include <cctype>

namespace codepilot {

RiskDetector::RiskDetector() {
  // 默认高风险命令（对齐文档第10.4节）
  dangerousCommands_ = {"git reset --hard", "git clean -fd", "git push", "dd",
                        "shutdown",         "reboot"};
  blockedCommands_ = {"sudo", "chmod -R 777", "chown -R", "mkfs", "rm -rf"};
}

void RiskDetector::setDangerousCommands(
    const std::vector<std::string> &commands) {
  dangerousCommands_ = commands;
}

void RiskDetector::setBlockedCommands(
    const std::vector<std::string> &commands) {
  blockedCommands_ = commands;
}

bool RiskDetector::matchesAny(const std::string &command,
                              const std::vector<std::string> &patterns) const {
  for (const auto &pattern : patterns) {
    if (command.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

RiskLevel RiskDetector::detectCommand(const std::string &command) const {
  // 1. 检查是否匹配直接阻止的命令
  // 针对 "rm -rf" 做精确匹配（避免误杀 "rm -rf temp" 以外的 rm 命令）
  if (command.find("rm -rf") != std::string::npos ||
      command.find("rm -fr") != std::string::npos ||
      command.find("rm --recursive --force") != std::string::npos) {
    return RiskLevel::Blocked;
  }

  if (matchesAny(command, blockedCommands_)) {
    return RiskLevel::Blocked;
  }

  // 2. 检查是否匹配高风险命令
  if (matchesAny(command, dangerousCommands_)) {
    return RiskLevel::Dangerous;
  }

  // 3. 检查管道 curl/wget -> bash（高风险）
  if (command.find("curl") != std::string::npos &&
      command.find("|") != std::string::npos &&
      (command.find("bash") != std::string::npos ||
       command.find("sh") != std::string::npos)) {
    return RiskLevel::Dangerous;
  }

  if (command.find("wget") != std::string::npos &&
      command.find("|") != std::string::npos &&
      (command.find("bash") != std::string::npos ||
       command.find("sh") != std::string::npos)) {
    return RiskLevel::Dangerous;
  }

  // 4. 默认视为安全
  return RiskLevel::Safe;
}

RiskLevel RiskDetector::detectToolCall(const std::string &toolName,
                                       const json &arguments) const {
  // shell.run 命令检测
  if (toolName == "shell.run" && arguments.contains("command")) {
    return detectCommand(arguments["command"].get<std::string>());
  }

  // file.write 默认 medium
  if (toolName == "file.write") {
    return RiskLevel::Medium;
  }

  // file.apply_patch 默认 medium
  if (toolName == "file.apply_patch") {
    return RiskLevel::Medium;
  }

  // git.status / git.diff / file.list / file.read / cd / pwd 默认 safe
  return RiskLevel::Safe;
}

} // namespace codepilot
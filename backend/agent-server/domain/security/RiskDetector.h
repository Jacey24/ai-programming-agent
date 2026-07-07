#pragma once

#include "domain/tools/Tool.h"
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// RiskDetector — 危险操作检测器
//
// 职责：
//   检测高风险 Shell 命令、Git 操作等
//   返回 RiskLevel 供 PermissionManager 决策
//
// 对齐整体架构说明.md 第10.4节
// ============================================================
class RiskDetector {
public:
  RiskDetector();

  // --- 检测命令风险等级 ---
  RiskLevel detectCommand(const std::string &command) const;

  // --- 检测工具调用风险等级（结合参数）---
  RiskLevel detectToolCall(const std::string &toolName,
                           const json &arguments) const;

  // --- 配置高风险命令列表（可从配置文件加载）---
  void setDangerousCommands(const std::vector<std::string> &commands);
  void setBlockedCommands(const std::vector<std::string> &commands);

private:
  std::vector<std::string> dangerousCommands_;
  std::vector<std::string> blockedCommands_;

  // 检查命令是否匹配危险模式列表
  bool matchesAny(const std::string &command,
                  const std::vector<std::string> &patterns) const;
};

} // namespace codepilot
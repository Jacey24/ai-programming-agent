#pragma once

#include <string>

namespace codepilot {

// ============================================================
// PermissionPolicyEngine — 按 Workspace 的权限策略引擎
//
// 职责：
//   1. 从 workspace.permissions_config (JSON) 解析策略
//   2. 根据 toolName 匹配策略，返回执行动作
//
// 策略类型：
//   - "auto_approve" → 直接放行，不产生权限请求
//   - "deny"          → 直接拒绝，不产生权限请求
//   - "ask"           → 走标准 PermissionManager 流程（默认）
//   - "*"             → 未匹配的 fallback 策略
//
// 纯函数，无状态，与 PermissionManager 互不侵入。
// 前端 API 协议完全不变。
// ============================================================

enum class PolicyAction {
  AutoApprove, // 自动同意
  Deny,        // 自动拒绝
  Ask          // 走标准权限请求流程
};

class PermissionPolicyEngine {
public:
  // 根据 permissions_config JSON 和 toolName 评估策略
  // permissions_config: 如 {"ShellTool": "auto_approve", "*": "ask"}
  // toolName: 如 "ShellTool", "FileTool"
  // 返回对应的 PolicyAction
  static PolicyAction evaluate(const std::string &permissions_config,
                               const std::string &toolName);
};

} // namespace codepilot
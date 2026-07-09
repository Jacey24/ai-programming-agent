#pragma once

#include "domain/tools/Tool.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace codepilot {

// ============================================================
// 断点类型
// before_tool_call: 工具执行前暂停，可查看/修改参数
// after_tool_call: 工具执行后暂停，可查看/修改结果
// ============================================================
enum class BreakpointType { BeforeToolCall, AfterToolCall };

inline std::string breakpointTypeToString(BreakpointType type) {
  switch (type) {
  case BreakpointType::BeforeToolCall:
    return "before_tool_call";
  case BreakpointType::AfterToolCall:
    return "after_tool_call";
  default:
    return "unknown";
  }
}

inline BreakpointType breakpointTypeFromString(const std::string &s) {
  if (s == "before_tool_call")
    return BreakpointType::BeforeToolCall;
  if (s == "after_tool_call")
    return BreakpointType::AfterToolCall;
  return BreakpointType::BeforeToolCall;
}

// ============================================================
// 调试器状态
// ============================================================
enum class DebugState {
  Disabled, // 调试器未启用（默认）
  Idle,     // 已启用但无断点命中
  Paused,   // 已暂停在某断点
  Stepping, // 单步中（执行完当前工具后再暂停）
  Continue, // 继续执行中
};

inline std::string debugStateToString(DebugState state) {
  switch (state) {
  case DebugState::Disabled:
    return "disabled";
  case DebugState::Idle:
    return "idle";
  case DebugState::Paused:
    return "paused";
  case DebugState::Stepping:
    return "stepping";
  case DebugState::Continue:
    return "continue";
  default:
    return "unknown";
  }
}

// ============================================================
// 断点配置 — 用户设置的一个断点
// ============================================================
struct BreakpointConfig {
  std::string toolName; // 工具名，如 "shell.run"
  BreakpointType type;  // 断点类型
  bool enabled{true};   // 是否启用
  int hitCount{0};      // 已命中次数（仅统计）
};

// ============================================================
// 暂停上下文 — 断点命中时收集的快照
// pauseBeforeTool 和 pauseAfterTool 使用此结构传递信息
// 用户可以通过 API 修改其中的 modifiedArguments / modifiedResult
// ============================================================
struct PausedContext {
  std::string taskId;
  std::string toolName;
  BreakpointType breakpointType{BreakpointType::BeforeToolCall};

  // BeforeToolCall 时使用
  json originalArguments; // 原始参数
  json modifiedArguments; // 用户可修改（初始等于 original）

  // AfterToolCall 时使用
  ToolResult originalResult;
  ToolResult modifiedResult;

  // 用户选择的动作
  std::string action;   // "continue" / "step_over" / "skip"
  bool skipTool{false}; // true 时跳过执行
  ToolResult skipResult;

  // 序列化为 JSON（用于 EventBus 事件）
  json serialize() const {
    json j;
    j["task_id"] = taskId;
    j["tool_name"] = toolName;
    j["breakpoint_type"] = breakpointTypeToString(breakpointType);
    if (!originalArguments.is_null()) {
      j["original_arguments"] = originalArguments;
      j["modified_arguments"] = modifiedArguments;
    }
    j["action"] = action;
    return j;
  }
};

} // namespace codepilot
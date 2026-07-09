#pragma once

#include "BreakpointTypes.h"
#include "domain/tools/Tool.h"
#include "event/EventBus.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// Debugger — 工具级调试器
//
// 职责：
//   1. 管理断点（设置/清除/查询）
//   2. 在 ToolSystem::callTool() 中拦截工具调用
//   3. 通过 condition_variable 阻塞等待用户指令
//   4. 通过 EventBus 发布断点事件（前端的 SSE 消费）
//
// 设计原则：
//   - 默认关闭（enabled_ = false），开启后才检查断点
//   - 完全被动：只监听外部 API 调用，不主动调度
//   - 依赖已存在的 EventBus 和 condition_variable 机制
//   - PermissionManager 的 waitForResolution 复用相同模式
//
// 线程安全：
//   - sessions_ 用 mutex 保护
//   - breakpoints_ 用 mutex 保护
//   - enabled_ 用 atomic
// ============================================================
class Debugger {
public:
  explicit Debugger(std::shared_ptr<EventBus> eventBus);

  // ============================================================
  // 启停控制
  // ============================================================
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }

  // ============================================================
  // 断点管理
  // ============================================================
  void setBreakpoint(const std::string &toolName, BreakpointType type);
  void removeBreakpoint(const std::string &toolName, BreakpointType type);
  void clearBreakpoints();
  bool hasBreakpoint(const std::string &toolName, BreakpointType type) const;
  std::vector<BreakpointConfig> listBreakpoints() const;

  // ============================================================
  // ★ 由 ToolSystem::callTool() 调用（在 Agent 线程中阻塞）
  // ============================================================

  // --- 工具执行前断点 ---
  // 如果对应工具设置了 BeforeToolCall 断点，阻塞等待用户指令
  // 参数: taskId, toolName, arguments（原始参数）
  // 返回: 用户可能修改后的参数；如果用户选择跳过，返回的 json
  //        包含 "__skip__": true 标记
  json pauseBeforeTool(const std::string &taskId, const std::string &toolName,
                       const json &arguments);

  // --- 工具执行后断点 ---
  // 如果对应工具设置了 AfterToolCall 断点，阻塞等待用户指令
  // 参数: taskId, toolName, arguments, result（原始结果）
  // 返回: 用户可能修改后的结果
  ToolResult pauseAfterTool(const std::string &taskId,
                            const std::string &toolName, const json &arguments,
                            const ToolResult &result);

  // ============================================================
  // ★ 由郑嘉娴的 API Controller 调用（跨线程唤醒）
  // ============================================================

  // --- 继续执行 ---
  // 唤醒 pauseBeforeTool/pauseAfterTool 中的等待线程
  void doContinue(const std::string &taskId);

  // --- 单步执行 ---
  // 执行完当前工具后，在下一个工具前再次暂停
  void doStepOver(const std::string &taskId);

  // --- 跳过当前工具 ---
  // 绕过工具执行直接返回 mockResult
  void doSkip(const std::string &taskId, const ToolResult &mockResult);

  // --- 修改工具参数（仅 BeforeToolCall 有效） ---
  // 不唤醒线程，只是修改内存中的参数
  // 调用 doContinue 后使用修改后的参数
  void modifyArguments(const std::string &taskId, const json &newArgs);

  // --- 修改工具结果（仅 AfterToolCall 有效） ---
  // 不唤醒线程，只是修改内存中的结果
  void modifyResult(const std::string &taskId, const ToolResult &newResult);

  // ============================================================
  // 状态查询
  // ============================================================
  DebugState getState(const std::string &taskId) const;
  PausedContext getPausedContext(const std::string &taskId) const;
  std::string getStateString(const std::string &taskId) const;

private:
  // ============================================================
  // 内部调试会话（每个 task 一个）
  // ============================================================
  struct DebugSession {
    DebugState state{DebugState::Disabled};

    // 当前断点信息
    BreakpointType pausedAt{BreakpointType::BeforeToolCall};
    std::string pausedToolName;

    // BeforeToolCall 上下文
    json originalArgs;
    json modifiedArgs;

    // AfterToolCall 上下文
    ToolResult originalResult;
    ToolResult modifiedResult;

    // 跳过标记
    bool skipRequested{false};
    ToolResult skipResult;

    // 同步原语（和 PermissionManager 相同模式）
    mutable std::mutex mtx;
    std::condition_variable cv;
    bool resolved{false};
    std::string controlAction; // "continue" / "step_over" / "skip"
  };

  std::atomic<bool> enabled_{false};

  mutable std::mutex breakpointsMutex_;
  std::vector<BreakpointConfig> breakpoints_;

  mutable std::mutex sessionsMutex_;
  std::unordered_map<std::string, std::shared_ptr<DebugSession>> sessions_;

  std::shared_ptr<EventBus> eventBus_;

  // --- 辅助方法 ---
  std::shared_ptr<DebugSession> getOrCreateSession(const std::string &taskId);
  DebugSession *findSession(const std::string &taskId) const;

  void publishBreakpointEvent(const std::string &taskId,
                              const std::string &toolName, BreakpointType type,
                              const json &metadata);

  void publishResolvedEvent(const std::string &taskId,
                            const std::string &toolName,
                            const std::string &action);
};

} // namespace codepilot
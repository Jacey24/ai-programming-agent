#include "Debugger.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace codepilot {

Debugger::Debugger(std::shared_ptr<EventBus> eventBus) : eventBus_(eventBus) {}

// ============================================================
// 启停控制
// ============================================================
void Debugger::setEnabled(bool enabled) {
  enabled_.store(enabled, std::memory_order_relaxed);
  if (!enabled) {
    // 禁用时清理所有会话并唤醒所有等待的线程
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto &[id, session] : sessions_) {
      std::lock_guard<std::mutex> slock(session->mtx);
      if (!session->resolved) {
        session->resolved = true;
        session->controlAction = "continue";
        session->cv.notify_all();
      }
    }
    sessions_.clear();
  }
}

// ============================================================
// 断点管理
// ============================================================
void Debugger::setBreakpoint(const std::string &toolName, BreakpointType type) {
  std::lock_guard<std::mutex> lock(breakpointsMutex_);
  // 检查是否已存在相同断点
  for (auto &bp : breakpoints_) {
    if (bp.toolName == toolName && bp.type == type) {
      bp.enabled = true;
      return;
    }
  }
  breakpoints_.push_back({toolName, type, true, 0});
}

void Debugger::removeBreakpoint(const std::string &toolName,
                                BreakpointType type) {
  std::lock_guard<std::mutex> lock(breakpointsMutex_);
  breakpoints_.erase(std::remove_if(breakpoints_.begin(), breakpoints_.end(),
                                    [&](const BreakpointConfig &bp) {
                                      return bp.toolName == toolName &&
                                             bp.type == type;
                                    }),
                     breakpoints_.end());
}

void Debugger::clearBreakpoints() {
  std::lock_guard<std::mutex> lock(breakpointsMutex_);
  breakpoints_.clear();
}

bool Debugger::hasBreakpoint(const std::string &toolName,
                             BreakpointType type) const {
  std::lock_guard<std::mutex> lock(breakpointsMutex_);
  for (const auto &bp : breakpoints_) {
    if (bp.toolName == toolName && bp.type == type && bp.enabled) {
      return true;
    }
  }
  return false;
}

std::vector<BreakpointConfig> Debugger::listBreakpoints() const {
  std::lock_guard<std::mutex> lock(breakpointsMutex_);
  return breakpoints_;
}

// ============================================================
// 辅助：获取或创建调试会话
// ============================================================
std::shared_ptr<Debugger::DebugSession>
Debugger::getOrCreateSession(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(sessionsMutex_);
  auto it = sessions_.find(taskId);
  if (it != sessions_.end()) {
    return it->second;
  }
  auto session = std::make_shared<DebugSession>();
  session->state = DebugState::Idle;
  sessions_[taskId] = session;
  return session;
}

Debugger::DebugSession *Debugger::findSession(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(sessionsMutex_);
  auto it = sessions_.find(taskId);
  if (it == sessions_.end())
    return nullptr;
  return it->second.get();
}

// ============================================================
// ★ pauseBeforeTool — 工具执行前断点（阻塞）
// ============================================================
json Debugger::pauseBeforeTool(const std::string &taskId,
                               const std::string &toolName,
                               const json &arguments) {
  auto session = getOrCreateSession(taskId);

  // 更新断点命中计数
  {
    std::lock_guard<std::mutex> lock(breakpointsMutex_);
    for (auto &bp : breakpoints_) {
      if (bp.toolName == toolName &&
          bp.type == BreakpointType::BeforeToolCall) {
        bp.hitCount++;
      }
    }
  }

  // 准备暂停上下文
  session->state = DebugState::Paused;
  session->pausedAt = BreakpointType::BeforeToolCall;
  session->pausedToolName = toolName;
  session->originalArgs = arguments;
  session->modifiedArgs = arguments;
  session->skipRequested = false;
  session->resolved = false;
  session->controlAction = "";

  // 发布断点命中事件
  json meta;
  meta["tool_name"] = toolName;
  meta["breakpoint_type"] = "before_tool_call";
  meta["arguments"] = arguments;
  publishBreakpointEvent(taskId, toolName, BreakpointType::BeforeToolCall,
                         meta);

  // 阻塞等待用户指令
  std::unique_lock<std::mutex> lock(session->mtx);
  session->cv.wait(lock, [session]() { return session->resolved; });

  // 根据用户指令决定行为
  if (session->skipRequested) {
    // 用户选择"跳过"
    json result;
    result["__skip__"] = true;
    result["__skip_output__"] = session->skipResult.output;
    result["__skip_success__"] = session->skipResult.success;
    // 更新状态
    session->state = DebugState::Continue;
    return result;
  }

  // 继续执行（使用用户可能修改的参数）
  session->state = (session->controlAction == "step_over")
                       ? DebugState::Stepping
                       : DebugState::Continue;
  return session->modifiedArgs;
}

// ============================================================
// ★ pauseAfterTool — 工具执行后断点（阻塞）
// ============================================================
ToolResult Debugger::pauseAfterTool(const std::string &taskId,
                                    const std::string &toolName,
                                    const json &arguments,
                                    const ToolResult &result) {
  auto session = getOrCreateSession(taskId);

  // 更新断点命中计数
  {
    std::lock_guard<std::mutex> lock(breakpointsMutex_);
    for (auto &bp : breakpoints_) {
      if (bp.toolName == toolName && bp.type == BreakpointType::AfterToolCall) {
        bp.hitCount++;
      }
    }
  }

  // 准备暂停上下文
  session->state = DebugState::Paused;
  session->pausedAt = BreakpointType::AfterToolCall;
  session->pausedToolName = toolName;
  session->originalResult = result;
  session->modifiedResult = result;
  session->skipRequested = false;
  session->resolved = false;
  session->controlAction = "";

  // 发布断点命中事件
  json meta;
  meta["tool_name"] = toolName;
  meta["breakpoint_type"] = "after_tool_call";
  meta["arguments"] = arguments;
  meta["result_success"] = result.success;
  meta["result_output"] = result.output.substr(0, 500);
  publishBreakpointEvent(taskId, toolName, BreakpointType::AfterToolCall, meta);

  // 阻塞等待用户指令
  std::unique_lock<std::mutex> lock(session->mtx);
  session->cv.wait(lock, [session]() { return session->resolved; });

  // 更新状态
  session->state = (session->controlAction == "step_over")
                       ? DebugState::Stepping
                       : DebugState::Continue;

  return session->modifiedResult;
}

// ============================================================
// ★ doContinue — 继续执行（跨线程唤醒）
// ============================================================
void Debugger::doContinue(const std::string &taskId) {
  auto *session = findSession(taskId);
  if (!session)
    return;

  std::lock_guard<std::mutex> lock(session->mtx);
  if (!session->resolved) {
    session->resolved = true;
    session->controlAction = "continue";
    session->cv.notify_all();
  }
  publishResolvedEvent(taskId, session->pausedToolName, "continue");
}

// ============================================================
// ★ doStepOver — 单步执行（跨线程唤醒）
// ============================================================
void Debugger::doStepOver(const std::string &taskId) {
  auto *session = findSession(taskId);
  if (!session)
    return;

  std::lock_guard<std::mutex> lock(session->mtx);
  if (!session->resolved) {
    session->resolved = true;
    session->controlAction = "step_over";
    session->cv.notify_all();
  }
  publishResolvedEvent(taskId, session->pausedToolName, "step_over");
}

// ============================================================
// ★ doSkip — 跳过当前工具（跨线程唤醒）
// ============================================================
void Debugger::doSkip(const std::string &taskId, const ToolResult &mockResult) {
  auto *session = findSession(taskId);
  if (!session)
    return;

  std::lock_guard<std::mutex> lock(session->mtx);
  if (!session->resolved) {
    session->skipRequested = true;
    session->skipResult = mockResult;
    session->resolved = true;
    session->controlAction = "skip";
    session->cv.notify_all();
  }
  publishResolvedEvent(taskId, session->pausedToolName, "skip");
}

// ============================================================
// ★ modifyArguments — 修改工具参数（不唤醒）
// ============================================================
void Debugger::modifyArguments(const std::string &taskId, const json &newArgs) {
  auto *session = findSession(taskId);
  if (!session)
    return;

  std::lock_guard<std::mutex> lock(session->mtx);
  session->modifiedArgs = newArgs;
}

// ============================================================
// ★ modifyResult — 修改工具结果（不唤醒）
// ============================================================
void Debugger::modifyResult(const std::string &taskId,
                            const ToolResult &newResult) {
  auto *session = findSession(taskId);
  if (!session)
    return;

  std::lock_guard<std::mutex> lock(session->mtx);
  session->modifiedResult = newResult;
}

// ============================================================
// 状态查询
// ============================================================
DebugState Debugger::getState(const std::string &taskId) const {
  auto *session = findSession(taskId);
  if (!session)
    return DebugState::Disabled;
  return session->state;
}

PausedContext Debugger::getPausedContext(const std::string &taskId) const {
  PausedContext ctx;
  auto *session = findSession(taskId);
  if (!session || session->state != DebugState::Paused) {
    return ctx;
  }

  ctx.taskId = taskId;
  ctx.toolName = session->pausedToolName;
  ctx.breakpointType = session->pausedAt;
  ctx.originalArguments = session->originalArgs;
  ctx.modifiedArguments = session->modifiedArgs;
  ctx.originalResult = session->originalResult;
  ctx.modifiedResult = session->modifiedResult;
  ctx.skipTool = session->skipRequested;
  ctx.skipResult = session->skipResult;
  ctx.action = session->controlAction;
  return ctx;
}

std::string Debugger::getStateString(const std::string &taskId) const {
  auto *session = findSession(taskId);
  if (!session)
    return "disabled";
  return debugStateToString(session->state);
}

// ============================================================
// 事件发布
// ============================================================
void Debugger::publishBreakpointEvent(const std::string &taskId,
                                      const std::string &toolName,
                                      BreakpointType type,
                                      const json &metadata) {
  if (!eventBus_)
    return;

  std::string content = "工具 " + toolName + " 已暂停于 " +
                        breakpointTypeToString(type) + " 断点";
  eventBus_->publish(
      EventData::Create(taskId, EventType::AgentMessage, content, metadata));
}

void Debugger::publishResolvedEvent(const std::string &taskId,
                                    const std::string &toolName,
                                    const std::string &action) {
  if (!eventBus_)
    return;

  std::string content = "断点已恢复: 用户选择了 " + action;
  json meta;
  meta["tool_name"] = toolName;
  meta["action"] = action;
  eventBus_->publish(
      EventData::Create(taskId, EventType::AgentMessage, content, meta));
}

} // namespace codepilot
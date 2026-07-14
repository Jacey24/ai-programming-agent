#pragma once

#include "AgentLoop.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// FrontendRegistration — 前端注册信息
// ============================================================
struct FrontendRegistration {
  int id;
  std::string type; // "web" / "tui" / "gui" / "custom"
};

// ============================================================
// ActiveTaskState — 活跃任务状态快照（新前端同步用）
// ============================================================
struct ActiveTaskState {
  std::string taskId;
  std::string globalId;
  std::string workspaceId;
  std::string goal;
  std::string currentExpert;
  std::string currentStage; // expert_start / waiting / received / tool / ...
  std::vector<std::string> expertChain;
  std::string status; // running / completed / failed / cancelled
  bool terminalEventSent{false};
};

// ============================================================
// AgentOrchestrator — 高级 Agent 编排器（单例）
//
// v2 变更：
//   - 移除 parentTaskId 三态解析（Task 独立，不再串联）
//   - 移除 continueTask（不再续接任务）
//   - sessionId → globalId
//   - 启动时自动创建默认 Global
//   - Task 完成时自动归档 summary/plan/output 到 global_context
//
// 职责：
//   1. AgentLoop 生命周期管理（创建/运行/取消）
//   2. 多前端注册
//   3. 系统状态快照
// ============================================================
class AgentOrchestrator {
public:
  static AgentOrchestrator &getInstance();
  AgentOrchestrator(const AgentOrchestrator &) = delete;
  AgentOrchestrator &operator=(const AgentOrchestrator &) = delete;

  // ── 初始化 ──
  void init(const std::string &expertConfigPath);

  // ── 前端注册 ──
  int registerFrontend(const std::string &type);
  void unregisterFrontend(int id);
  int frontendCount() const;

  // ── 任务生命周期 ──
  // globalId: 归属的 Global ID (g_xxx)
  //   若为空或不存在，自动使用默认 Global
  void startTask(const std::string &taskId, const std::string &globalId,
                 const std::string &workspaceId, const std::string &goal,
                 const TaskRunOptions &options);

  // 取消指定任务
  bool cancelTask(const std::string &taskId);

  // ── 状态快照 ──
  std::vector<ActiveTaskState> activeTasks() const;
  ActiveTaskState getTaskState(const std::string &taskId) const;

  // ── 查询 ──
  bool isReady() const;
  bool isReady(ExecutionMode mode) const;

private:
  AgentOrchestrator() = default;

  // 解析 globalId：若为空则使用默认 Global
  std::string resolveGlobalId(const std::string &globalId);

  // 异步执行
  void runTaskThread(const std::string &taskId, const std::string &globalId,
                     const std::string &workspaceId, const std::string &goal,
                     TaskRunOptions options,
                     std::shared_ptr<std::atomic<bool>> cancelFlag);

  AgentLoopResult runDirectAnswer(
      const std::string &taskId, const std::string &globalId,
      const std::string &workspaceId, const std::string &goal,
      const std::shared_ptr<std::atomic<bool>> &cancelFlag);

  // 任务完成后的收尾（SSE + DB + global_context 归档 + 内存清理）
  void finalizeTask(const std::string &taskId, const std::string &globalId,
                    const AgentLoopResult &result);

  mutable std::mutex mutex_;

  std::string expertConfigPath_;
  bool initialized_ = false;
  std::string defaultGlobalId_; // 启动时自动创建

  // 前端注册
  int nextFrontendId_ = 1;
  std::vector<FrontendRegistration> frontends_;

  // 活跃任务
  std::unordered_map<std::string, ActiveTaskState> activeTasks_;
  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
      cancelFlags_;
  std::unordered_map<std::string, std::thread> taskThreads_;
};

} // namespace codepilot

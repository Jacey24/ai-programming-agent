#pragma once

#include "AgentConfiguration.h"
#include "AgentLoopResumer.h"
#include "PlanManager.h"
#include "TaskContext.h"
#include "TaskRunOptions.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace codepilot {

// ============================================================
// AgentLoopResult — AgentLoop 的返回结果
// ============================================================
struct AgentLoopResult {
  std::string finalOutput;
  std::string status; // "completed" / "failed" / "config_error" / "cancelled" /
                      // "paused"
  Plan finalPlan;
  std::string summary;
  std::vector<std::string> expertChain;
  bool finalOutputSent{false};
};

// ============================================================
// AgentLoop — 纯沙盒主循环
//
// v3 变更：
//   - 新增 runFromSnapshot / setOnPause 用于用户主动暂停/恢复
//   - 权限暂停通过 PermissionManager::waitForResolution() 阻塞实现
//
// v2 变更：
//   - 移除 parentTaskId 机制（Task 独立，跨任务上下文通过 Global 检索）
//   - 移除 runContinue()（不再续接任务）
//   - sessionId → globalId
//   - 首次进入 Expert 时从 Global 知识库检索上下文注入
// ============================================================
class AgentLoop {
public:
  AgentLoop(const std::string &configPath);

  // 运行完整 Agent 流程
  AgentLoopResult run(const std::string &taskId, const std::string &globalId,
                      const std::string &workspaceId, const std::string &goal,
                      const TaskRunOptions &options,
                      std::shared_ptr<std::atomic<bool>> cancelFlag = nullptr);

  // Resume 专用：从快照恢复执行
  AgentLoopResult
  runFromSnapshot(const TaskSnapshot &snapshot,
                  std::shared_ptr<std::atomic<bool>> cancelFlag = nullptr);

  // 设置暂停回调（AgentOrchestrator 注册以保存快照）
  void setOnPause(std::function<void(const TaskSnapshot &)> callback) {
    onPause_ = std::move(callback);
  }

  bool isReady() const;

private:
  std::string configPath_;
  std::function<void(const TaskSnapshot &)> onPause_;

  AgentLoopResult runExpertChain(const std::string &taskId,
                                 const std::string &globalId,
                                 const std::string &workspaceId,
                                 TaskContext &ctx,
                                 const ExpertConfig *entryExpert,
                                 const TaskRunOptions &options,
                                 std::shared_ptr<std::atomic<bool>> cancelFlag,
                                 const std::string &initialSessionHistory = "");

  AgentLoopResult finalizeWithCriticalSummary(const std::string &taskId,
                                              const std::string &reason,
                                              const std::string &status,
                                              const std::string &sessionHistory,
                                              PlanManager &planMgr,
                                              const TaskContext &ctx) const;
};

} // namespace codepilot

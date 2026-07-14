#pragma once

#include "AgentConfiguration.h"
#include "PlanManager.h"
#include "TaskContext.h"
#include "TaskRunOptions.h"
#include <atomic>
#include <memory>
#include <string>

namespace codepilot {

// ============================================================
// AgentLoopResult — AgentLoop 的返回结果
// ============================================================
struct AgentLoopResult {
  std::string finalOutput;
  std::string status; // "completed" / "failed" / "config_error" / "cancelled"
  Plan finalPlan;
  std::string summary;
  std::vector<std::string> expertChain;
};

// ============================================================
// AgentLoop — 纯沙盒主循环
//
// v2 变更：
//   - 移除 parentTaskId 机制（Task 独立，跨任务上下文通过 Global 检索）
//   - 移除 runContinue()（不再续接任务）
//   - sessionId → globalId
//   - 首次进入 Expert 时从 Global 知识库检索上下文注入
//
// 配置管理：通过 AgentConfiguration 单例
//   每轮 LLM 调用前执行 reconfigure() 确保配置最新
//   LLM 调用优先使用 Expert 自身的 _llm_* 配置，降级到全局默认
// ============================================================
class AgentLoop {
public:
  // configPath: Expert JSON 配置文件路径（如 "config/experts.json"）
  AgentLoop(const std::string &configPath);

  // 运行完整 Agent 流程
  // globalId: 归属的 Global ID（用于 global_context 检索）
  AgentLoopResult run(const std::string &taskId, const std::string &globalId,
                      const std::string &workspaceId, const std::string &goal,
                      const TaskRunOptions &options,
                      std::shared_ptr<std::atomic<bool>> cancelFlag = nullptr);

  // 检查配置是否加载成功
  bool isReady() const;

private:
  std::string configPath_;

  // Expert Chain 主循环
  // initialSessionHistory: Resume 时传入的历史对话（正常启动传空字符串）
  AgentLoopResult runExpertChain(const std::string &taskId,
                                 const std::string &globalId,
                                 const std::string &workspaceId,
                                 TaskContext &ctx,
                                 const ExpertConfig *entryExpert,
                                 const TaskRunOptions &options,
                                 std::shared_ptr<std::atomic<bool>> cancelFlag,
                                 const std::string &initialSessionHistory = "");

  // ── Critical Exit 兜底 ──
  // 所有非正常的 chain 终止路径（轮次耗尽/路由失败/切换超限等）
  // 调用 LLM 单次总结后返回，绝不重入 Chain、绝不递归
  AgentLoopResult finalizeWithCriticalSummary(const std::string &taskId,
                                              const std::string &reason,
                                              const std::string &status,
                                              const std::string &sessionHistory,
                                              PlanManager &planMgr,
                                              const TaskContext &ctx) const;
};

} // namespace codepilot

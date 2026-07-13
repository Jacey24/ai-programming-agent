#include "application/AgentService.h"
#include "application/ToolSystem.h"
#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"

#include "domain/agent/AgentOrchestrator.h"
#include "domain/agent/Planner.h"
#include "domain/agent/RoleRegistry.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>

namespace codepilot {

AgentResult AgentService::runTask(const std::string &taskId,
                                  const std::string &sessionId,
                                  const std::string &workspaceId,
                                  const std::string &goal,
                                  const TaskRunOptions &options) {
  // ============================================================
  // ★ 通过 AgentOrchestrator 执行任务
  // ============================================================
  auto &orch = AgentOrchestrator::getInstance();
  if (orch.isReady()) {
    // sessionId 在 v2 中作为 globalId 兼容传递
    // orchestrator 内部异步执行 AgentLoop，但 AgentService 需要同步返回结果
    AgentLoop agentLoop("config/experts.json");
    AgentLoopResult loopResult =
        agentLoop.run(taskId, sessionId, workspaceId, goal);

    AgentResult result;
    result.taskId = taskId;
    result.sessionId = sessionId;
    result.workspaceId = workspaceId;
    result.goal = goal;
    result.status = loopResult.status;
    result.planJson = loopResult.finalPlan.toPromptFragment();
    result.currentStep =
        loopResult.status == "completed" ? "completed" : "failed";
    {
      auto now = std::chrono::system_clock::now();
      auto t = std::chrono::system_clock::to_time_t(now);
      std::tm *tm = std::gmtime(&t);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
      result.updatedAt = buf;
    }
    result.createdAt = result.updatedAt;
    result.logs = loopResult.expertChain;
    return result;
  }

  // ============================================================
  // 降级：走旧 Agent 主循环
  // ============================================================
  RoleRegistry registry;
  registry.loadFromFile("config/agent_roles.json");
  Planner planner(registry);
  Agent agent(registry, planner);

  std::string toolsDesc = getToolsDescription();
  agent.setToolsDescription(toolsDesc);

  // LLM 客户端由 LlmClientFacade 单例自动管理，无需手动注入

  AgentConfig config;
  config.maxSteps = std::clamp(options.maxSteps, 1, 20);
  config.maxRoundsPerStep = std::clamp(options.maxRoundsPerStep, 1, 6);
  config.autoRunSafeCommands = options.autoRunSafeCommands;
  config.requireFileWritePermission = options.requireFileWritePermission;
  agent.setConfig(config);

  const ExecutionMode mode = resolveExecutionMode(goal, options.mode);
  AgentResult result =
      mode == ExecutionMode::DirectAnswer
          ? agent.executeDirectAnswer(taskId, sessionId, workspaceId, goal)
          : agent.executeTask(taskId, sessionId, workspaceId, goal);
  result.currentStep =
      result.currentStep.empty() ? "completed" : result.currentStep;
  return result;
}

ExecutionMode AgentService::resolveExecutionMode(const std::string &goal,
                                                 ExecutionMode requestedMode) {
  if (requestedMode != ExecutionMode::Auto)
    return requestedMode;
  (void)goal;
  return ExecutionMode::WorkspaceAgent;
}

std::string AgentService::getToolsDescription() {
  if (!ToolSystem::getInstance().isInitialized()) {
    return "";
  }

  std::string desc;
  auto &registry = ToolSystem::getInstance().registry();
  auto names = registry.listToolNames();

  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0)
      desc += "\n";
    auto *tool = registry.getTool(names[i]);
    if (tool) {
      desc += "  - " + tool->name() + ": " + tool->description();
    }
  }

  return desc;
}

} // namespace codepilot
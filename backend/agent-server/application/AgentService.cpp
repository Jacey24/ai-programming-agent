#include "application/AgentService.h"
#include "application/ToolSystem.h"
#include "facade/LlmClientFacade.h"

#include "domain/agent/Planner.h"
#include "domain/agent/RoleRegistry.h"

#include <algorithm>
#include <memory>

namespace codepilot {

AgentResult AgentService::runTask(const std::string &taskId,
                                  const std::string &sessionId,
                                  const std::string &workspaceId,
                                  const std::string &goal,
                                  const TaskRunOptions &options) {
  RoleRegistry registry;
  registry.loadFromFile("config/agent_roles.json");
  Planner planner(registry);
  Agent agent(registry, planner);

  std::string toolsDesc = getToolsDescription();
  agent.setToolsDescription(toolsDesc);

  // LLM 客户端由 LlmClientFacade 单例自动管理，无需手动注入
  // 如有外部已注入的旧式 LlmClient，setLlmClient 标记为弃用且为空操作

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
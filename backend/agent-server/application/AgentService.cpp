#include "application/AgentService.h"

#include "domain/agent/Planner.h"
#include "domain/agent/RoleRegistry.h"

namespace codepilot {

AgentResult AgentService::runTask(
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    const std::string& goal) {
    RoleRegistry registry;
    registry.loadFromFile("config/agent_roles.json");
    Planner planner(registry);
    Agent agent(registry, planner);
    AgentResult result = agent.executeTask(taskId, sessionId, workspaceId, goal);
    result.status = "pending";
    result.currentStep = result.currentStep.empty() ? "pending execution" : result.currentStep;
    return result;
}

} // namespace codepilot

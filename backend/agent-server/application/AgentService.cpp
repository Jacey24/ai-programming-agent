#include "application/AgentService.h"
#include "application/ToolSystem.h"

#include "domain/agent/Planner.h"
#include "domain/agent/RoleRegistry.h"
#include "infrastructure/llm/LlmClient.h"
#include "infrastructure/llm/OpenAICompatibleClient.h"

#include <memory>

namespace codepilot {

AgentResult AgentService::runTask(
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    const std::string& goal,
    sqlite3* db) {
    RoleRegistry registry;
    registry.loadFromFile("config/agent_roles.json");
    Planner planner(registry);
    Agent agent(registry, planner);

    // Sprint 2: 注入工具描述给 Agent
    std::string toolsDesc = getToolsDescription();
    agent.setToolsDescription(toolsDesc);
    auto llmConfig = OpenAICompatibleClient::loadConfig("config/llm.json");
    if (OpenAICompatibleClient::isConfigured(llmConfig)) {
        agent.setLlmClient(std::make_shared<OpenAICompatibleClient>(llmConfig));
    } else {
        agent.setLlmClient(std::make_shared<MockLlmClient>());
    }
    // Sprint 2：注入数据库句柄，打通存储层（db 来自郑嘉娴 TaskController）
    if (db) {
        agent.setDb(db);
    }

    AgentResult result = agent.executeTask(taskId, sessionId, workspaceId, goal);
    result.currentStep = result.currentStep.empty() ? "completed" : result.currentStep;
    return result;
}

std::string AgentService::getToolsDescription() {
    if (!ToolSystem::getInstance().isInitialized()) {
        return "";
    }

    // 从 ToolRegistry 获取所有工具的摘要
    std::string desc;
    auto& registry = ToolSystem::getInstance().registry();
    auto names = registry.listToolNames();

    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) desc += "\n";
        auto* tool = registry.getTool(names[i]);
        if (tool) {
            desc += "  - " + tool->name() + ": " + tool->description();
        }
    }

    return desc;
}

} // namespace codepilot

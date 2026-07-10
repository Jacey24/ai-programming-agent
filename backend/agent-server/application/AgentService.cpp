#include "application/AgentService.h"
#include "application/ToolSystem.h"

#include "domain/agent/Planner.h"
#include "domain/agent/RoleRegistry.h"
#include "infrastructure/llm/LlmClient.h"
#include "infrastructure/llm/OpenAICompatibleClient.h"

#include <memory>
#include <algorithm>
#include <cctype>

namespace codepilot {

AgentResult AgentService::runTask(
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    const std::string& goal,
    sqlite3* db,
    const TaskRunOptions& options) {
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

    AgentConfig config;
    config.maxSteps = std::clamp(options.maxSteps, 1, 20);
    config.maxRoundsPerStep = std::clamp(options.maxRoundsPerStep, 1, 6);
    config.autoRunSafeCommands = options.autoRunSafeCommands;
    config.requireFileWritePermission = options.requireFileWritePermission;
    agent.setConfig(config);

    const ExecutionMode mode = resolveExecutionMode(goal, options.mode);
    AgentResult result = mode == ExecutionMode::DirectAnswer
        ? agent.executeDirectAnswer(taskId, sessionId, workspaceId, goal)
        : agent.executeTask(taskId, sessionId, workspaceId, goal);
    result.currentStep = result.currentStep.empty() ? "completed" : result.currentStep;
    return result;
}

ExecutionMode AgentService::resolveExecutionMode(const std::string& goal, ExecutionMode requestedMode) {
    if (requestedMode != ExecutionMode::Auto) return requestedMode;
    std::string lower = goal;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::vector<std::string> workspaceSignals = {
        "创建文件", "修改这个项目", "修复代码", "重构", "运行测试", "编译", "workspace", "写入", "create file", "modify", "fix ", "refactor", "run test", "compile"};
    for (const auto& signal : workspaceSignals) if (lower.find(signal) != std::string::npos) return ExecutionMode::WorkspaceAgent;
    const std::vector<std::string> fileExtensions = {".py", ".cpp", ".cc", ".c", ".h", ".hpp", ".ts", ".tsx", ".js", ".json", ".md"};
    for (const auto& extension : fileExtensions) if (lower.find(extension) != std::string::npos) return ExecutionMode::WorkspaceAgent;
    const std::vector<std::string> answerSignals = {
        "输出一段", "给出", "解释", "什么是", "算法示例", "output", "example", "explain", "what is"};
    for (const auto& signal : answerSignals) if (lower.find(signal) != std::string::npos) return ExecutionMode::DirectAnswer;
    return ExecutionMode::DirectAnswer;
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

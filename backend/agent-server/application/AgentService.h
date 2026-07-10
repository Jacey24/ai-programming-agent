#pragma once

#include "domain/agent/Agent.h"

#include <memory>
#include <string>

namespace codepilot {

enum class ExecutionMode { Auto, DirectAnswer, WorkspaceAgent };

struct TaskRunOptions {
    ExecutionMode mode{ExecutionMode::Auto};
    bool autoRunSafeCommands{true};
    bool requireFileWritePermission{true};
    int maxSteps{6};
    int maxRoundsPerStep{3};
};

class AgentService {
public:
    AgentResult runTask(
        const std::string& taskId,
        const std::string& sessionId,
        const std::string& workspaceId,
        const std::string& goal,
        sqlite3* db = nullptr,
        const TaskRunOptions& options = {});

    static ExecutionMode resolveExecutionMode(const std::string& goal, ExecutionMode requestedMode);

    // Sprint 2：获取 Agent 实例，供外部使用 buildExecutorPrompt 等
    Agent* getAgent() { return agent_.get(); }

    // Sprint 2：获取工具描述文本（由 ToolSystem 提供）
    static std::string getToolsDescription();

private:
    std::unique_ptr<Agent> agent_;
};

} // namespace codepilot

#pragma once

#include "RoleRegistry.h"
#include "TaskQueue.h"
#include "TaskState.h"

#include <string>
#include <vector>

namespace codepilot {

class Planner;

struct AgentConfig {
    int maxSteps = 20;
    int toolTimeoutSeconds = 120;
};

struct AgentResult {
    std::string taskId;
    std::string sessionId;
    std::string workspaceId;
    std::string goal;
    std::string status;
    std::string planJson;
    std::string currentStep;
    std::string createdAt;
    std::string updatedAt;
};

class Agent {
public:
    Agent(RoleRegistry& registry, Planner& planner);

    AgentResult executeTask(
        const std::string& taskId,
        const std::string& sessionId,
        const std::string& workspaceId,
        const std::string& goal);

private:
    std::string toolsToString(const std::vector<std::string>& tools);
    std::string planToJson(const std::vector<PlanStep>& steps);
    std::string escapeJson(const std::string& s);
    std::string iso8601Now();

    RoleRegistry& registry_;
    Planner& planner_;
    TaskQueue queue_;
    AgentConfig config_;
    std::vector<std::string> context_;
};

} // namespace codepilot
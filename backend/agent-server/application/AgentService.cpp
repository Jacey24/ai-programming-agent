#include "../domain/agent/TaskQueue.h"
#include "../domain/agent/TaskState.h"

#include <string>

namespace codepilot {

class Agent;
class RoleRegistry;

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

class AgentService {
public:
    AgentResult runTask(const std::string& taskId,
                        const std::string& sessionId,
                        const std::string& workspaceId,
                        const std::string& goal) {
        AgentResult result;
        result.taskId = taskId;
        result.sessionId = sessionId;
        result.workspaceId = workspaceId;
        result.goal = goal;
        result.status = "created";
        result.planJson = "[\"分析任务需求\",\"检查项目结构\",\"制定执行方案\",\"执行任务操作\",\"验证最终结果\"]";
        result.currentStep = "尚未开始";
        result.createdAt = "2026-07-07T00:00:00Z";
        result.updatedAt = "2026-07-07T00:00:00Z";
        return result;
    }
};

} // namespace codepilot
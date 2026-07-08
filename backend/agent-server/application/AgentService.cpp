#include "../domain/agent/Agent.h"
#include "../domain/agent/RoleRegistry.h"
#include "../domain/agent/Planner.cpp"
#include <string>

namespace codepilot {

class AgentService {
public:
    AgentResult runTask(const std::string& taskId,
                        const std::string& sessionId,
                        const std::string& workspaceId,
                        const std::string& goal) {
        RoleRegistry registry;
        registry.loadFromFile("config/agent_roles.json");
        Planner planner(registry);
        Agent agent(registry, planner);
        return agent.executeTask(taskId, sessionId, workspaceId, goal);
    }
};

} // namespace codepilot
#pragma once

#include "domain/agent/Agent.h"

#include <string>

namespace codepilot {

class AgentService {
public:
    AgentResult runTask(
        const std::string& taskId,
        const std::string& sessionId,
        const std::string& workspaceId,
        const std::string& goal);
};

} // namespace codepilot

#pragma once

#include "RoleRegistry.h"
#include "TaskQueue.h"
#include <string>
#include <vector>

namespace codepilot {

class Planner {
public:
    Planner(RoleRegistry& registry) : registry_(registry) {}

    std::vector<PlanStep> generatePlan(const std::string& goal);

    const RoleRegistry& getRegistry() const { return registry_; }

private:
    RoleRegistry& registry_;
};

} // namespace codepilot
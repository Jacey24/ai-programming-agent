#include "Planner.h"

#include <string>
#include <vector>

namespace codepilot {

std::vector<PlanStep> Planner::generatePlan(const std::string& goal) {
    std::vector<PlanStep> steps;
    steps.push_back({"coder", "分析任务需求"});
    steps.push_back({"coder", "检查项目结构"});
    steps.push_back({"coder", "制定执行方案"});
    steps.push_back({"coder", "执行任务操作"});
    steps.push_back({"coder", "验证最终结果"});
    return steps;
}

} // namespace codepilot
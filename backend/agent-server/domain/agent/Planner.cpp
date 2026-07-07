#include "RoleRegistry.h"
#include "TaskQueue.h"

#include <string>
#include <vector>

namespace codepilot {

class Planner {
public:
    Planner(RoleRegistry& registry) : registry_(registry) {}

    // Sprint 1：写死返回默认计划；Sprint 2：调 PM 角色调 LLM 生成
    std::vector<PlanStep> generatePlan(const std::string& goal) {
        std::vector<PlanStep> steps;
        steps.push_back({"coder", "分析任务需求"});
        steps.push_back({"coder", "检查项目结构"});
        steps.push_back({"coder", "制定执行方案"});
        steps.push_back({"coder", "执行任务操作"});
        steps.push_back({"coder", "验证最终结果"});
        return steps;
    }

    // 获取可用角色名列表（供 Agent 查询）
    const RoleRegistry& getRegistry() const { return registry_; }

private:
    RoleRegistry& registry_;
};

} // namespace codepilot
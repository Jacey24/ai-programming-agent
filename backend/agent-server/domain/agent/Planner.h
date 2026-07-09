#pragma once

#include "RoleRegistry.h"
#include "TaskQueue.h"
#include <string>
#include <vector>

namespace codepilot {

class Planner {
public:
    Planner(RoleRegistry& registry) : registry_(registry) {}

    // Sprint 1 兼容：硬编码降级
    std::vector<PlanStep> generatePlan(const std::string& goal);

    // Sprint 2：构建规划 Prompt（由 LLM 调用方使用）
    // goal: 用户任务目标
    // toolsDesc: 可用工具描述文本（由 ToolSystem 提供）
    // 返回: 完整的 system prompt + user message 格式
    std::string buildPlanningPrompt(
        const std::string& goal,
        const std::string& toolsDesc) const;

    // Sprint 2：从 LLM 返回的 <plan> XML 中解析步骤列表
    static std::vector<PlanStep> parsePlanFromResponse(
        const std::string& rawResponse);

    const RoleRegistry& getRegistry() const { return registry_; }

private:
    RoleRegistry& registry_;

    // 硬编码降级（Sprint 1）
    static std::vector<PlanStep> fallbackPlan(const std::string& goal);
};

} // namespace codepilot
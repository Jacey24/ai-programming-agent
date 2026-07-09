#include "Planner.h"
#include "ResponseParser.h"

#include <string>
#include <vector>

namespace codepilot {

std::vector<PlanStep> Planner::generatePlan(const std::string& goal) {
    // Sprint 1/2 兼容：
    // - 如果外部通过 buildPlanningPrompt + parsePlanFromResponse
    //   已经完成了 AI 规划，则调用 parsePlanFromResponse
    // - 否则降级为硬编码计划
    return fallbackPlan(goal);
}

std::string Planner::buildPlanningPrompt(
    const std::string& goal,
    const std::string& toolsDesc) const {
    std::string prompt;

    // System Prompt
    prompt += "你是一个任务规划专家（Planner）。你的职责是理解用户的任务目标，"
             "并将其分解为一系列可执行的步骤。\n\n";

    prompt += "## 规划规则\n";
    prompt += "1. 每个步骤应该是原子性的，描述清楚做什么\n";
    prompt += "2. 步骤之间保持逻辑顺序：先分析理解，再执行操作，最后验证结果\n";
    prompt += "3. 如果任务简单（如读取文件），1-3 步即可；"
             "复杂任务（如跨文件重构）可以 5-8 步\n";
    prompt += "4. 优先使用可用工具来完成操作\n\n";

    // 可用工具列表
    prompt += "## 可用工具\n";
    if (!toolsDesc.empty()) {
        prompt += toolsDesc + "\n\n";
    } else {
        prompt += "（无可用工具信息）\n\n";
    }

    // 输出格式约束
    prompt += "## 输出格式\n";
    prompt += "你必须严格按照以下 XML 格式输出计划：\n\n";
    prompt += "<plan>\n";
    prompt += "  <task skill=\"executor\">第一步：具体操作描述</task>\n";
    prompt += "  <task skill=\"executor\">第二步：具体操作描述</task>\n";
    prompt += "  ...\n";
    prompt += "</plan>\n\n";

    prompt += "skill 字段可选值：executor（默认）、txt（文本处理）、"
             "sys（系统操作）\n";
    prompt += "不需要在计划中输出 DONE 标记，计划只是步骤列表。\n\n";

    // 用户任务
    prompt += "## 用户任务\n";
    prompt += goal + "\n\n";
    prompt += "请输出你的执行计划：";

    return prompt;
}

std::vector<PlanStep> Planner::parsePlanFromResponse(
    const std::string& rawResponse) {
    // 使用 ResponseParser 的 parseAll 来提取 planSteps
    ParsedResponse parsed = ResponseParser::parseAll(rawResponse);
    if (!parsed.planSteps.empty()) {
        return parsed.planSteps;
    }

    return fallbackPlan(""); // 降级
}

std::vector<PlanStep> Planner::fallbackPlan(const std::string& goal) {
    std::vector<PlanStep> steps;
    steps.push_back({"executor", "分析任务需求"});
    steps.push_back({"executor", "检查项目结构"});
    if (!goal.empty()) {
        steps.push_back({"executor", "执行核心操作: " + goal});
    }
    steps.push_back({"executor", "验证执行结果"});
    return steps;
}

} // namespace codepilot
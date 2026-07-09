#include "PromptAdapter.h"

namespace codepilot {

std::string PromptAdapter::buildPrompt(
    LlmProviderType /*provider*/,
    const RoleConfig& role,
    const std::string& /*goal*/,
    const std::string& currentStep,
    const std::string& context,
    const std::string& toolsDesc)
{
    // Sprint 1/2 兼容：通用固定格式
    std::string prompt;
    prompt += "[" + role.name + "] " + role.description + "\n";
    prompt += "当前步骤: " + currentStep + "\n";
    prompt += "上下文: " + context + "\n";
    if (!toolsDesc.empty()) {
        prompt += "可用工具: " + toolsDesc + "\n";
    }
    prompt += "[系统] Sprint 2 执行模式\n";
    return prompt;
}

std::string PromptAdapter::buildRolePrompt(
    PromptRole promptRole,
    LlmProviderType /*provider*/,
    const std::string& goal,
    const std::string& toolsDesc,
    const std::string& context,
    const std::string& extraInput)
{
    switch (promptRole) {
        case PromptRole::Planner:
            return buildPlannerPrompt(goal, toolsDesc);

        case PromptRole::Executor:
            return buildExecutorPrompt(goal, toolsDesc, context, extraInput);

        case PromptRole::Summarizer:
            return buildSummarizerPrompt(goal, context, extraInput);

        case PromptRole::Generic:
        default:
            return buildExecutorPrompt(goal, toolsDesc, context, extraInput);
    }
}

std::string PromptAdapter::buildPlannerPrompt(
    const std::string& goal,
    const std::string& toolsDesc)
{
    std::string prompt;

    // System Prompt — Planner 角色定义
    prompt += "你是 CodePilot 的任务规划专家（Planner）。\n";
    prompt += "你的职责是理解用户的任务目标，分析可用的工具能力，"
             "将复杂的编程任务分解为有序的执行步骤。\n\n";

    // 规划规则
    prompt += "## 规划规则\n";
    prompt += "1. 先理解任务目标，再决定步骤\n";
    prompt += "2. 每个步骤应该是原子性的，描述清楚\"做什么\"（不需要描述\"怎么做\"）\n";
    prompt += "3. 步骤顺序要逻辑合理：分析→执行→验证\n";
    prompt += "4. 简单任务（如：读取某个文件）1-2 步；中等任务 3-5 步；复杂任务 6-10 步\n";
    prompt += "5. 优先利用可用工具来减少步骤数\n";
    prompt += "6. 为高风险操作（如删除、覆盖文件、执行危险命令）添加验证步骤\n\n";

    // 可用工具列表
    prompt += "## 可用工具\n";
    if (!toolsDesc.empty()) {
        prompt += toolsDesc + "\n";
    } else {
        prompt += "（无可用工具信息，将生成通用步骤）\n";
    }
    prompt += "\n";

    // 输出格式约束
    prompt += "## 输出格式（严格遵守）\n";
    prompt += "你必须仅输出一个完整的 <plan> XML 块，不要添加任何解释文字。\n\n";
    prompt += "格式如下：\n";
    prompt += "<plan>\n";
    prompt += "  <task skill=\"executor\">第一步描述</task>\n";
    prompt += "  <task skill=\"executor\">第二步描述</task>\n";
    prompt += "  <task skill=\"executor\" fallback=\"true\">如果失败则执行此步骤</task>\n";
    prompt += "</plan>\n\n";

    prompt += "skill 属性可选值：\n";
    prompt += "  - executor：通用执行者（默认）\n";
    prompt += "  - txt：文本/代码处理\n";
    prompt += "  - sys：系统操作\n";
    prompt += "fallback=\"true\"：该步骤仅在上一任务失败时执行（可选）\n\n";

    // 用户任务
    prompt += "## 用户任务\n";
    prompt += goal + "\n\n";
    prompt += "请输出你的执行计划：";

    return prompt;
}

std::string PromptAdapter::buildExecutorPrompt(
    const std::string& goal,
    const std::string& toolsDesc,
    const std::string& context,
    const std::string& currentStep)
{
    std::string prompt;

    // System Prompt — Executor 角色定义
    prompt += "你是 CodePilot 的任务执行者（Executor）。\n";
    prompt += "你的职责是逐步执行给定的编程任务，使用工具来读取文件、运行命令、修改代码。\n\n";

    // 任务目标
    prompt += "## 任务目标\n";
    prompt += goal + "\n\n";

    // 当前步骤
    prompt += "## 当前步骤\n";
    prompt += currentStep + "\n\n";

    // 上下文（已完成步骤 + 工具结果）
    prompt += "## 已完成的上下文\n";
    if (context.empty()) {
        prompt += "（这是第一步，无历史上下文）\n";
    } else {
        prompt += context + "\n";
    }
    prompt += "\n";

    // 可用工具
    prompt += "## 可用工具\n";
    if (!toolsDesc.empty()) {
        prompt += toolsDesc + "\n";
    } else {
        prompt += "（无工具信息可用）\n";
    }
    prompt += "\n";

    // 输出格式约束
    prompt += "## 输出格式（严格遵守）\n";
    prompt += "你必须使用以下 XML 标签输出操作：\n\n";
    prompt += "<cmd>TOOL_NAME 参数</cmd>\n";
    prompt += "  - 调用一个工具\n";
    prompt += "  - 可以在一轮中输出多个 <cmd>，它们将按顺序执行\n";
    prompt += "  - 格式：<cmd>工具名 参数值</cmd>\n\n";

    prompt += "<invoke server=\"MCP服务器\" tool=\"工具名\" args='{\"key\":\"value\"}'/>\n";
    prompt += "  - 调用 MCP 外部服务工具\n\n";

    prompt += "DONE: 完成总结\n";
    prompt += "  - 当当前步骤完成时使用\n";
    prompt += "  - 在 DONE: 后面用一句话总结完成了什么\n\n";

    prompt += "FAIL: 失败原因\n";
    prompt += "  - 当步骤无法继续时使用\n";
    prompt += "  - 简洁描述失败原因\n\n";

    prompt += "## 重要规则\n";
    prompt += "1. 每一步只能输出 <cmd>/<invoke> 或 DONE/FAIL，不能同时输出两者\n";
    prompt += "2. 如果工具返回了错误信息，先尝试理解错误原因再决定下一步\n";
    prompt += "3. 操作文件前应该先读取文件了解当前内容\n";
    prompt += "4. 修改代码后应该验证修改结果\n\n";

    prompt += "请输出你的操作：";

    return prompt;
}

std::string PromptAdapter::buildSummarizerPrompt(
    const std::string& goal,
    const std::string& context,
    const std::string& resultsSummary)
{
    std::string prompt;

    prompt += "你是 CodePilot 的任务汇总者（Summarizer）。\n";
    prompt += "你的职责是将执行过程中的所有步骤结果汇总为简洁的自然语言回复。\n\n";

    prompt += "## 原始任务\n";
    prompt += goal + "\n\n";

    prompt += "## 执行结果\n";
    if (!resultsSummary.empty()) {
        prompt += resultsSummary + "\n\n";
    }

    if (!context.empty()) {
        prompt += "## 详细上下文（供参考）\n";
        prompt += context + "\n\n";
    }

    prompt += "请生成一个简短的汇总报告，描述：\n";
    prompt += "1. 完成了什么\n";
    prompt += "2. 关键的发现或修改\n";
    prompt += "3. 如果失败，说明原因和建议\n\n";
    prompt += "输出格式：纯文本回复，用于展示给用户。";

    return prompt;
}

} // namespace codepilot
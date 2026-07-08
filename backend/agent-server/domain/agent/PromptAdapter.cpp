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
    // Sprint 1：忽略 provider，返回固定格式假 Prompt
    // Sprint 2：根据 provider 选择不同构造策略
    std::string prompt;
    prompt += "[" + role.name + "] " + role.description + "\n";
    prompt += "当前步骤: " + currentStep + "\n";
    prompt += "上下文: " + context + "\n";
    if (!toolsDesc.empty()) {
        prompt += "可用工具: " + toolsDesc + "\n";
    }
    prompt += "[系统] Sprint 1 假执行模式";
    return prompt;
}

} // namespace codepilot
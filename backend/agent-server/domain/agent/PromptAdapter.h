#pragma once

#include "LlmProvider.h"
#include "RoleRegistry.h"
#include <string>

namespace codepilot {

// Sprint 2：Prompt 角色类型
enum class PromptRole {
    Planner,
    Executor,
    Summarizer,
    Generic
};

class PromptAdapter {
public:
    // Sprint 1 兼容：通用固定格式
    static std::string buildPrompt(
        LlmProviderType provider,
        const RoleConfig& role,
        const std::string& goal,
        const std::string& currentStep,
        const std::string& context,
        const std::string& toolsDesc);

    // Sprint 2：根据角色类型构建不同的 Prompt
    static std::string buildRolePrompt(
        PromptRole promptRole,
        LlmProviderType provider,
        const std::string& goal,
        const std::string& toolsDesc,
        const std::string& context,
        const std::string& extraInput = "");

private:
    // 各角色 Prompt 模板
    static std::string buildPlannerPrompt(
        const std::string& goal,
        const std::string& toolsDesc);

    static std::string buildExecutorPrompt(
        const std::string& goal,
        const std::string& toolsDesc,
        const std::string& context,
        const std::string& currentStep);

    static std::string buildSummarizerPrompt(
        const std::string& goal,
        const std::string& context,
        const std::string& resultsSummary);
};

} // namespace codepilot
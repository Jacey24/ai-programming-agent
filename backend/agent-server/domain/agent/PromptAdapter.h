#pragma once

#include "LlmProvider.h"
#include "RoleRegistry.h"
#include <string>

namespace codepilot {

class PromptAdapter {
public:
    // Sprint 1：返回固定假 Prompt；Sprint 2：根据 provider 真实构造
    static std::string buildPrompt(
        LlmProviderType provider,
        const RoleConfig& role,
        const std::string& goal,
        const std::string& currentStep,
        const std::string& context,
        const std::string& toolsDesc
    );
};

} // namespace codepilot
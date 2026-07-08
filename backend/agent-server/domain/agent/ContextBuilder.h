#pragma once

#include "RoleRegistry.h"
#include <string>
#include <vector>

namespace codepilot {

class ContextBuilder {
public:
    // Sprint 1：简单拼接上下文；Sprint 2：按轮次精细化构造
    static std::string buildRound(
        const std::vector<std::string>& history,
        const RoleConfig& role,
        const std::string& currentStep,
        const std::vector<std::string>& visibleTools
    );

    // 将历史上下文数组格式化为 LLM 可读文本
    static std::string formatHistory(const std::vector<std::string>& context);
};

} // namespace codepilot
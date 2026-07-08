#include "ContextBuilder.h"

namespace codepilot {

std::string ContextBuilder::buildRound(
    const std::vector<std::string>& history,
    const RoleConfig& role,
    const std::string& currentStep,
    const std::vector<std::string>& visibleTools)
{
    std::string result;
    result += "=== 第 " + std::to_string(history.size() / 2 + 1) + " 轮 ===\n";
    result += "角色: " + role.name + " (" + role.description + ")\n";
    result += "当前步骤: " + currentStep + "\n";

    if (!visibleTools.empty()) {
        result += "可见工具: ";
        for (size_t i = 0; i < visibleTools.size(); ++i) {
            if (i > 0) result += ", ";
            result += visibleTools[i];
        }
        result += "\n";
    }

    result += "--- 历史上下文 ---\n";
    for (const auto& line : history) {
        result += line + "\n";
    }
    result += "--- 本轮开始 ---";

    return result;
}

std::string ContextBuilder::formatHistory(const std::vector<std::string>& context) {
    std::string result;
    for (size_t i = 0; i < context.size(); ++i) {
        result += "[" + std::to_string(i) + "] " + context[i] + "\n";
    }
    return result;
}

} // namespace codepilot
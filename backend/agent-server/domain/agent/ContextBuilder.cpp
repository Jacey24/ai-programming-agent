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

std::string ContextBuilder::buildRoundLimited(
    const std::vector<std::string>& history,
    const RoleConfig& role,
    const std::string& currentStep,
    const std::vector<std::string>& visibleTools,
    size_t maxHistoryLines)
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

    if (history.size() > maxHistoryLines) {
        // 裁剪：汇总旧记录，保留最近行
        result += "[摘要] 前 " + std::to_string(history.size() - maxHistoryLines)
                  + " 条上下文已省略...\n";
        for (size_t i = history.size() - maxHistoryLines; i < history.size(); ++i) {
            result += history[i] + "\n";
        }
    } else {
        for (const auto& line : history) {
            result += line + "\n";
        }
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

std::string ContextBuilder::summarizeHistory(
    const std::vector<std::string>& context,
    size_t recentLines) {
    if (context.empty()) return "";

    std::string result;
    const size_t total = context.size();

    if (total <= recentLines) {
        // 全部保留
        for (size_t i = 0; i < total; ++i) {
            result += context[i] + "\n";
        }
    } else {
        // 压缩前 N-recentLines 条为一行摘要
        result += "[历史摘要] 前 " + std::to_string(total - recentLines)
                  + " 轮工具调用和结果已省略\n";

        // 保留最近 recentLines 条
        for (size_t i = total - recentLines; i < total; ++i) {
            result += context[i] + "\n";
        }
    }

    return result;
}

} // namespace codepilot
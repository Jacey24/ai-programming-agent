#pragma once

#include "RoleRegistry.h"
#include <string>
#include <vector>

namespace codepilot {

class ContextBuilder {
public:
    // Sprint 1：简单拼接上下文
    static std::string buildRound(
        const std::vector<std::string>& history,
        const RoleConfig& role,
        const std::string& currentStep,
        const std::vector<std::string>& visibleTools
    );

    // Sprint 2：受限上下文构建（带裁剪，防止 token 溢出）
    // maxHistoryLines: 最多保留的历史行数
    static std::string buildRoundLimited(
        const std::vector<std::string>& history,
        const RoleConfig& role,
        const std::string& currentStep,
        const std::vector<std::string>& visibleTools,
        size_t maxHistoryLines = 20);

    // 将历史上下文数组格式化为 LLM 可读文本
    static std::string formatHistory(const std::vector<std::string>& context);

    // Sprint 2：将历史上下文压缩为摘要
    // 保留最近 N 行完整内容，其余压缩为一行摘要
    static std::string summarizeHistory(
        const std::vector<std::string>& context,
        size_t recentLines = 10);
};

// Sprint 2：上下文裁剪常量（参考 Astral 已验证值）
constexpr size_t MAX_PLAN_CTX = 3000;   // 规划对话历史上限（字符）
constexpr size_t MAX_RECENT = 1500;     // 最近对话参考上限（字符）

} // namespace codepilot
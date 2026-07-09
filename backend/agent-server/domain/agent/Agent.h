#pragma once

#include "RoleRegistry.h"
#include "TaskQueue.h"
#include "TaskState.h"
#include "ResponseParser.h"

#include "infrastructure/storage/repositories/LogRepository.h"

#include <memory>
#include <string>
#include <vector>

namespace codepilot {

class Planner;

struct AgentConfig {
    int maxSteps = 20;
    int toolTimeoutSeconds = 120;
    int maxRetries = 1;          // Sprint 2：失败最大重试次数
    bool enableDeadlockCheck = true;  // Sprint 2：死锁检测
};

struct AgentResult {
    std::string taskId;
    std::string sessionId;
    std::string workspaceId;
    std::string goal;
    std::string status;
    std::string planJson;
    std::string currentStep;
    std::string createdAt;
    std::string updatedAt;
};

class Agent {
public:
    Agent(RoleRegistry& registry, Planner& planner);

    // 设置可用工具描述（由外部 ToolSystem 提供）
    void setToolsDescription(const std::string& desc) { toolsDesc_ = desc; }

    // Sprint 2：注入数据库句柄，用于日志持久化（db 来自郑嘉娴 TaskController）
    void setDb(sqlite3* db) { db_ = db; }

    AgentResult executeTask(
        const std::string& taskId,
        const std::string& sessionId,
        const std::string& workspaceId,
        const std::string& goal);

    // Sprint 2：构建执行期 Prompt（给 LLM 调用方使用）
    // step: 当前步骤描述
    // role: 执行角色
    // 返回: 完整的 prompt 文本
    std::string buildExecutorPrompt(
        const PlanStep& step,
        const RoleConfig& role) const;

private:
    // 工具辅助
    std::string toolsToString(const std::vector<std::string>& tools);
    std::string planToJson(const std::vector<PlanStep>& steps);
    std::string escapeJson(const std::string& s);
    std::string iso8601Now();

    // Sprint 2：单步执行（构建 prompt → 模拟 LLM 响应 → 解析 → 工具调用/完成）
    // rawLlmOutput: [out] LLM 输出的原始文本（由外部 LLM 调用方填充）
    // 返回: 解析后的响应
    ParsedResponse executeSingleStep(
        const PlanStep& step,
        const RoleConfig& role,
        const std::string& goal,
        const std::string& taskId,
        const std::string& sessionId,
        const std::string& workspaceId,
        std::string& rawLlmOutput);

    // Sprint 2：死锁检测
    bool isDeadlock(const std::vector<ParsedCommand>& commands) const;

    RoleRegistry& registry_;
    Planner& planner_;
    TaskQueue queue_;
    AgentConfig config_;
    std::vector<std::string> context_;
    std::string toolsDesc_;  // 可用工具文本描述

    // 死锁检测状态
    std::vector<ParsedCommand> prevCommands_;
    int deadlockCount_ = 0;

    // Sprint 2：数据库句柄（日志持久化，db 来自郑嘉娴 TaskController）
    sqlite3* db_ = nullptr;
};

} // namespace codepilot

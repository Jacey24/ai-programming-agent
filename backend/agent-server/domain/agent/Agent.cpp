#include "Agent.h"
#include "ContextBuilder.h"
#include "LlmProvider.h"
#include "PromptAdapter.h"
#include "ResponseParser.h"
#include "Planner.h"

#include "infrastructure/storage/repositories/LogRepository.h"

#include "application/ToolSystem.h"
#include "domain/tools/Tool.h"

#include <chrono>
#include <ctime>
#include <sstream>

namespace codepilot {

Agent::Agent(RoleRegistry& registry, Planner& planner)
    : registry_(registry), planner_(planner) {}

AgentResult Agent::executeTask(
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    const std::string& goal)
{
    const std::string now = iso8601Now();
    context_.clear();
    prevCommands_.clear();
    deadlockCount_ = 0;

    // ============================================================
    // 阶段 1：规划 (Planner)
    // ============================================================
    // 构建规划 prompt → LLM 调用方处理 → parsePlanFromResponse 解析
    // 这里直接走 fallbackPlan 作为降级，同时把 prompt 暴露为 buildPlanningPrompt
    auto steps = planner_.generatePlan(goal);
    queue_.loadFromPlan(steps);

    context_.push_back("[plan] 已生成计划: " + planToJson(steps));

    // Sprint 2 打通存储层：记录规划到 execution_logs（钟经添 LogRepository）
    if (db_) {
        LogRepository(db_).createLog(taskId, "planning", planToJson(steps));
    }

    // ============================================================
    // 阶段 2：步骤执行 (Executor)
    // ============================================================
    while (queue_.hasNext() && static_cast<int>(context_.size()) < config_.maxSteps) {
        const PlanStep step = queue_.current();
        const RoleConfig* role = registry_.findByName(step.role);
        if (!role) {
            context_.push_back("[error] role not registered: " + step.role);
            queue_.markComplete();
            continue;
        }

        // 2a. 构建本步上下文
        std::string roundCtx = ContextBuilder::buildRound(
            context_, *role, step.action, role->visibleTools);
        context_.push_back("[round] " + roundCtx);

        // 2b. 构建执行期 Prompt（留接口，刘子恒后续填 LLM 调用）
        std::string prompt = buildExecutorPrompt(step, *role);
        context_.push_back("[prompt] " + prompt);

        // 2c. Sprint 2 占位: LLM 调用
        // TODO(刘子恒): 替换此 mock 为真实 LLM 调用
        std::string rawLlmOutput;
        ParsedResponse parsed = executeSingleStep(
            step, *role, goal, taskId, sessionId, workspaceId, rawLlmOutput);
        context_.push_back("[response] " + rawLlmOutput);

        // 2d. 死锁检测
        if (config_.enableDeadlockCheck && !parsed.commands.empty()) {
            if (isDeadlock(parsed.commands)) {
                deadlockCount_++;
                context_.push_back("[warning] deadlock detected #" +
                    std::to_string(deadlockCount_));
                if (deadlockCount_ >= 3) {
                    context_.push_back("[error] too many deadlocks, aborting step");
                    queue_.markComplete();
                    continue;
                }
            } else {
                deadlockCount_ = 0;
            }
            prevCommands_ = parsed.commands;
        }

        // 2e. 根据解析结果分派
        if (parsed.type == ResponseType::FinalAnswer || parsed.isDone) {
            // 步骤完成
            context_.push_back("[final] " + parsed.content);
            // Sprint 2：记录步骤完成日志（钟经添 LogRepository）
            if (db_) {
                LogRepository(db_).createLog(taskId, "step_done",
                    "步骤完成: " + step.action);
            }
            queue_.markComplete();
            continue;
        }

        if (parsed.type == ResponseType::ToolCall) {
            // 工具调用分支
            bool anyToolFailed = false;

            for (const auto& cmd : parsed.commands) {
                context_.push_back("[tool] calling: " + cmd.toolName +
                    (cmd.toolArgs.empty() ? "" : " with args: " + cmd.toolArgs));

                ToolContext toolCtx;
                toolCtx.taskId = taskId;
                toolCtx.sessionId = sessionId;
                toolCtx.workspaceId = workspaceId;

                ToolResult toolResult;
                try {
                    // 调用 ToolSystem 执行工具
                    if (ToolSystem::getInstance().isInitialized()) {
                        // 将文本参数转为 JSON（简化版）
                        json args;
                        if (!cmd.toolArgs.empty()) {
                            // 尝试解析 JSON 参数
                            args = json::parse(cmd.toolArgs, nullptr, false);
                            if (args.is_discarded()) {
                                // 非 JSON，作为 path 或 command 参数
                                args["input"] = cmd.toolArgs;
                            }
                        }

                        // Sprint 2：带权限检查的工具调用（周子涵 ToolSystem）
                        toolResult = ToolSystem::getInstance().callToolWithPermission(
                            cmd.toolName, toolCtx, args);
                    } else {
                        toolResult.success = true;
                        toolResult.output = "[mock] tool not initialized, " + cmd.toolName;
                    }
                } catch (const std::exception& e) {
                    toolResult.success = false;
                    toolResult.error = std::string("Tool call exception: ") + e.what();
                }

                // 结果追加到上下文
                std::ostringstream resultCtx;
                resultCtx << "[tool_result] " << cmd.toolName << ": ";
                if (toolResult.success) {
                    resultCtx << "success\n" << toolResult.output;
                } else {
                    resultCtx << "failed\n" << toolResult.error;
                    anyToolFailed = true;
                }
                context_.push_back(resultCtx.str());

                // Sprint 2：写入工具调用日志到 execution_logs（钟经添 LogRepository）
                if (db_) {
                    LogRepository(db_).createLog(
                        taskId,
                        toolResult.success ? "tool_result" : "tool_error",
                        resultCtx.str());
                }
            }

            // 工具调用后，继续本步骤的下一轮循环（不 markComplete）
            // 如果工具失败了，也继续让 LLM 看到失败信息后决策
            continue;
        }

        if (parsed.type == ResponseType::Plan) {
            // LLM 返回了新计划，插入到队列中
            context_.push_back("[plan_update] received new plan from LLM");
            for (const auto& newStep : parsed.planSteps) {
                queue_.insertAfterCurrent(newStep);
            }
            queue_.markComplete();
            continue;
        }

        if (parsed.isFail) {
            context_.push_back("[fail] " + parsed.failReason);
            // Sprint 2：记录步骤失败日志（钟经添 LogRepository）
            if (db_) {
                LogRepository(db_).createLog(taskId, "step_failed", parsed.failReason);
            }
            queue_.markComplete();
            continue;
        }

        // Unknown → 假定完成
        queue_.markComplete();
    }

    // ============================================================
    // 阶段 3：汇总 (Summarizer)
    // ============================================================
    // 将所有上下文摘要 + 最终状态汇总为自然语言
    // 通过 AgentService 调用方传递给前端

    const std::string doneTime = iso8601Now();

    AgentResult result;
    result.taskId = taskId;
    result.sessionId = sessionId;
    result.workspaceId = workspaceId;
    result.goal = goal;
    result.status = queue_.deriveStatusString();
    result.planJson = planToJson(steps);
    result.currentStep = steps.empty() ? "none" : steps.back().action;
    result.createdAt = now;
    result.updatedAt = doneTime;
    return result;
}

ParsedResponse Agent::executeSingleStep(
    const PlanStep& step,
    const RoleConfig& role,
    const std::string& goal,
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    std::string& rawLlmOutput)
{
    // Sprint 2: 构建 prompt 并模拟 LLM 响应
    // 真正的 LLM 调用由刘子恒在外部完成
    std::string prompt = PromptAdapter::buildPrompt(
        LlmProviderType::Generic, role, goal, step.action,
        ContextBuilder::formatHistory(context_),
        toolsDesc_.empty() ? toolsToString(role.visibleTools) : toolsDesc_);

    // Mock LLM 响应（占位，刘子恒替换此处）
    rawLlmOutput = "<cmd>LIST</cmd>\nDONE: 步骤 '" + step.action + "' 已完成（mock）";

    return ResponseParser::parseAll(rawLlmOutput);
}

std::string Agent::buildExecutorPrompt(
    const PlanStep& step,
    const RoleConfig& role) const {
    std::string prompt;

    // System Prompt — 来自角色配置
    prompt += "[" + role.name + "] " + role.description + "\n\n";

    // 上下文信息
    prompt += "## 系统信息\n";
    prompt += "当前工作区: workspace\n\n";

    // 已完成步骤摘要
    prompt += "## 已完成步骤\n";
    if (context_.empty()) {
        prompt += "（无）\n";
    } else {
        for (size_t i = 0; i < context_.size() && i < 10; ++i) {
            prompt += context_[i] + "\n";
        }
        if (context_.size() > 10) {
            prompt += "... (共 " + std::to_string(context_.size()) + " 条上下文记录)\n";
        }
    }
    prompt += "\n";

    // 当前任务
    prompt += "## 当前步骤\n";
    prompt += step.action + "\n\n";

    // 可用工具（按角色可见性过滤）
    prompt += "## 可用工具\n";
    if (!role.visibleTools.empty()) {
        for (const auto& t : role.visibleTools) {
            prompt += "  - " + t + "\n";
        }
    } else {
        prompt += "（无特殊工具限制）\n";
    }
    prompt += "\n";

    // 输出格式约束
    prompt += "## 输出格式\n";
    prompt += "请使用以下 XML 标签输出你的操作：\n\n";
    prompt += "<cmd>TOOL_NAME arguments...</cmd>\n";
    prompt += "  - 调用工具执行操作\n";
    prompt += "  - 可以一次输出多个 <cmd>\n\n";
    prompt += "DONE: 完成总结\n";
    prompt += "  - 当步骤完成时使用\n\n";
    prompt += "FAIL: 失败原因\n";
    prompt += "  - 当步骤无法继续时使用\n\n";
    prompt += "请输出你的操作：";

    return prompt;
}

bool Agent::isDeadlock(const std::vector<ParsedCommand>& commands) const {
    if (prevCommands_.empty() || commands.empty()) return false;
    if (commands.size() != prevCommands_.size()) return false;

    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].toolName != prevCommands_[i].toolName) return false;
        if (commands[i].toolArgs != prevCommands_[i].toolArgs) return false;
    }
    return true;
}

std::string Agent::toolsToString(const std::vector<std::string>& tools) {
    std::string s;
    for (size_t i = 0; i < tools.size(); ++i) {
        if (i > 0) s += ", ";
        s += tools[i];
    }
    return s;
}

std::string Agent::planToJson(const std::vector<PlanStep>& steps) {
    std::string json = "[";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + escapeJson(steps[i].action) + "\"";
    }
    json += "]";
    return json;
}

std::string Agent::escapeJson(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            default:   out += ch;
        }
    }
    return out;
}

std::string Agent::iso8601Now() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
    return buf;
}

} // namespace codepilot
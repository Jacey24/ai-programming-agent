#include "Agent.h"
#include "ContextBuilder.h"
#include "LlmProvider.h"
#include "PromptAdapter.h"
#include "ResponseParser.h"
#include "Planner.h"

#include "infrastructure/storage/repositories/LogRepository.h"
#include "infrastructure/storage/repositories/EventRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include "application/ToolSystem.h"
#include "domain/tools/Tool.h"
#include "event/EventBus.h"
#include "infrastructure/llm/LlmClient.h"

#include <atomic>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <nlohmann/json.hpp>
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
    publishTaskEvent(taskId, EventType::TaskPlanning, "Agent is planning the task");

    std::vector<PlanStep> steps;
    if (llmClient_) {
        const std::string planningPrompt =
            planner_.buildPlanningPrompt(goal, toolsDesc_);
        LlmResponse planningResponse =
            llmClient_->chat({planningPrompt, "", config_.toolTimeoutSeconds});
        if (planningResponse.success) {
            steps = Planner::parsePlanFromResponse(planningResponse.content);
            context_.push_back("[planning_response] " + planningResponse.content);
        } else {
            context_.push_back("[planning_fallback] " + planningResponse.error);
        }
    }
    if (steps.empty()) {
        steps = planner_.generatePlan(goal);
    }
    queue_.loadFromPlan(steps);

    context_.push_back("[plan] 已生成计划: " + planToJson(steps));

    // Sprint 2 打通存储层：记录规划到 execution_logs（钟经添 LogRepository）
    if (db_) {
        LogRepository(db_).createLog(taskId, "planning", planToJson(steps));
    }

    // ============================================================
    // 阶段 2：步骤执行 (Executor)
    // ============================================================
    // 用独立的迭代计数器控制上限，避免用 context_ 条数误判导致中途退出
    int loopIterations = 0;
    const int maxIterations = config_.maxSteps > 0 ? config_.maxSteps : 20;
    // 单步工具轮次上限：防止模型在同一步内无限探索（反复 ls/read）而不推进。
    const int maxRoundsPerStep = 6;
    std::string currentStepAction;
    int stepRounds = 0;
    std::string lastResponse;
    while (queue_.hasNext() && loopIterations < maxIterations) {
        ++loopIterations;
        const PlanStep step = queue_.current();
        // 跟踪当前步骤的连续轮次；步骤切换时重置计数。
        if (step.action != currentStepAction) {
            currentStepAction = step.action;
            stepRounds = 0;
        }
        ++stepRounds;
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

        // 记录最有意义的一段 LLM 输出，作为最终答案候选
        if (!rawLlmOutput.empty() &&
            rawLlmOutput.rfind("DONE:", 0) != 0 &&
            rawLlmOutput.size() > lastResponse.size()) {
            lastResponse = rawLlmOutput;
        }

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
                const std::string toolName = normalizeToolName(cmd.toolName);
                context_.push_back("[tool] calling: " + toolName +
                    (cmd.toolArgs.empty() ? "" : " with args: " + cmd.toolArgs));

                ToolContext toolCtx;
                toolCtx.taskId = taskId;
                toolCtx.sessionId = sessionId;
                toolCtx.workspaceId = workspaceId;
                // 让 shell.run / git.* 等工具在工作区根目录下执行，
                // 与 file.* 工具的相对路径视图保持一致（否则会落到进程 cwd /app）。
                if (ToolSystem::getInstance().isInitialized()) {
                    toolCtx.workspacePath =
                        ToolSystem::getInstance().workspace().rootPath();
                }

                json toolArgs = buildToolArguments(toolName, cmd.toolArgs);

                // Sprint 2：工具开始事件落库到 task_events（周子涵持久化落库）
                persistTaskEvent(taskId, EventType::ToolStarted,
                    "Calling tool: " + toolName,
                    json{{"tool_name", toolName}, {"arguments", toolArgs}});

                ToolResult toolResult;
                try {
                    // 调用 ToolSystem 执行工具
                    if (ToolSystem::getInstance().isInitialized()) {
                        toolResult = ToolSystem::getInstance().callToolWithPermission(
                            toolName, toolCtx, toolArgs);
                    } else {
                        toolResult.success = true;
                        toolResult.output = "[mock] tool not initialized, " + toolName;
                    }
                } catch (const std::exception& e) {
                    toolResult.success = false;
                    toolResult.error = std::string("Tool call exception: ") + e.what();
                }

                // 结果追加到上下文
                std::ostringstream resultCtx;
                resultCtx << "[tool_result] " << toolName << ": ";
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

                // Sprint 2：工具调用落库到 tool_calls（周子涵持久化落库）
                persistToolCall(taskId, toolName, toolArgs, toolResult);

                // Sprint 2：工具完成事件落库到 task_events（周子涵持久化落库）
                persistTaskEvent(taskId, EventType::ToolFinished,
                    toolResult.success ? ("Tool finished: " + toolName)
                                       : ("Tool failed: " + toolName),
                    json{{"tool_name", toolName},
                         {"success", toolResult.success},
                         {"exit_code", toolResult.exitCode}});
            }

            // 工具调用后，继续本步骤的下一轮循环（不 markComplete）
            // 如果工具失败了，也继续让 LLM 看到失败信息后决策
            // 但若同一步内工具轮次过多（模型在无限探索），强制完成本步以推进任务。
            if (stepRounds >= maxRoundsPerStep) {
                context_.push_back(
                    "[step_forced_done] 步骤 '" + step.action +
                    "' 已达单步工具轮次上限，自动完成并进入下一步");
                if (db_) {
                    LogRepository(db_).createLog(taskId, "step_done",
                        "步骤达到轮次上限自动完成: " + step.action);
                }
                queue_.markComplete();
            }
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
            // 步骤失败（含 LLM 调用失败）→ 任务整体失败，避免误报 completed
            queue_.markFailed();
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
    result.logs = context_;
    // 把最终答案带进完成事件，供前端实时展示（而不仅仅是状态）
    const std::string finalContent =
        result.status == "completed"
            ? (lastResponse.empty() ? std::string("Agent task completed") : lastResponse)
            : std::string("Agent task failed");
    publishTaskEvent(taskId,
        result.status == "completed" ? EventType::TaskCompleted : EventType::TaskFailed,
        finalContent,
        "{\"status\":\"" + escapeJson(result.status) + "\"}");
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
    // 使用带输出格式约束（<cmd>/DONE/FAIL）的执行期 Prompt，
    // 与记录到上下文的 [prompt] 保持一致，避免模型照抄历史里的 <plan> 结构。
    std::string prompt = buildExecutorPrompt(step, role);

    // Prefer the configured LLM client; fall back to a deterministic local result.
    if (llmClient_) {
        LlmResponse response =
            llmClient_->chat({prompt, "", config_.toolTimeoutSeconds});
        if (response.success && !response.content.empty()) {
            rawLlmOutput = response.content;
            return ResponseParser::parseAll(rawLlmOutput);
        }
        // LLM 调用失败（如额度耗尽 SetLimitExceeded、网络错误等）：如实标记步骤失败，
        // 不再伪造 DONE 让任务假装成功完成。
        const std::string reason =
            response.error.empty() ? "LLM 未返回内容" : response.error;
        context_.push_back("[llm_fallback] " + reason);
        rawLlmOutput = "FAIL: LLM 调用失败: " + reason;
        return ResponseParser::parseAll(rawLlmOutput);
    }

    // 未配置 LLM 客户端（纯 mock/测试模式）：返回确定性完成结果。
    rawLlmOutput = "DONE: step '" + step.action + "' completed by mock fallback";

    return ResponseParser::parseAll(rawLlmOutput);
}

namespace {

// 为可见工具提供简明的参数用法提示，帮助模型正确构造 <cmd> 调用。
std::string describeToolUsage(const std::string& tool) {
    if (tool == "file.list")
        return " —— 列出目录。参数: {\"path\":\".\",\"depth\":2}";
    if (tool == "file.read")
        return " —— 读取文件。参数: {\"path\":\"a.py\"}";
    if (tool == "file.write")
        return " —— 覆盖写入文件（创建/写入代码首选）。参数: "
               "{\"path\":\"a.py\",\"content\":\"...\"}";
    if (tool == "file.apply_patch")
        return " —— 对已有文件应用 diff 补丁。参数: "
               "{\"file_path\":\"a.py\",\"patch\":\"<unified diff>\"}";
    if (tool == "shell.run")
        return " —— 执行 shell 命令。参数: {\"command\":\"ls -la\"}";
    if (tool == "git.status")
        return " —— 查看 git 状态（无参数）";
    if (tool == "git.diff")
        return " —— 查看 git diff（无参数）";
    return "";
}

} // namespace

std::string Agent::buildExecutorPrompt(
    const PlanStep& step,
    const RoleConfig& role) const {
    std::string prompt;

    // System Prompt — 来自角色配置
    prompt += "[" + role.name + "] " + role.description + "\n\n";

    // 上下文信息
    prompt += "## 系统信息\n";
    prompt += "当前工作区: workspace（所有相对路径基于工作区根目录）\n\n";

    // 已完成步骤摘要：过滤掉规划噪声，仅保留工具结果/最终答案/错误，取最近若干条，
    // 避免历史里的 <plan> 结构污染上下文导致模型照抄或重复调用。
    prompt += "## 已完成的操作与结果\n";
    {
        std::vector<std::string> relevant;
        for (const auto& entry : context_) {
            if (entry.rfind("[tool_result]", 0) == 0 ||
                entry.rfind("[tool]", 0) == 0 ||
                entry.rfind("[final]", 0) == 0 ||
                entry.rfind("[error]", 0) == 0) {
                relevant.push_back(entry);
            }
        }
        if (relevant.empty()) {
            prompt += "（尚无已完成操作）\n";
        } else {
            const size_t maxShow = 8;
            const size_t start =
                relevant.size() > maxShow ? relevant.size() - maxShow : 0;
            for (size_t i = start; i < relevant.size(); ++i) {
                std::string line = relevant[i];
                if (line.size() > 500) {
                    line = line.substr(0, 500) + " …(截断)";
                }
                prompt += line + "\n";
            }
        }
    }
    prompt += "\n";

    // 当前任务
    prompt += "## 当前步骤\n";
    prompt += step.action + "\n\n";

    // 可用工具（按角色可见性过滤，附带参数用法）
    prompt += "## 可用工具（只能使用以下工具）\n";
    if (!role.visibleTools.empty()) {
        for (const auto& t : role.visibleTools) {
            prompt += "  - " + t + describeToolUsage(t) + "\n";
        }
    } else {
        prompt += "（无特殊工具限制）\n";
    }
    prompt += "\n";

    // 输出格式约束
    prompt += "## 输出格式（严格遵守）\n";
    prompt += "调用工具时用 <cmd> 标签，参数使用 JSON 对象，例如创建代码文件：\n";
    prompt += "<cmd>file.write {\"path\":\"bubble_sort.py\",\"content\":\""
              "def bubble_sort(a):\\n    ...\"}</cmd>\n";
    prompt += "  - content 内的换行写作 \\n，双引号写作 \\\"\n";
    prompt += "  - 一次可输出多个 <cmd>\n";
    prompt += "  - 已成功完成的操作不要重复调用\n";
    prompt += "  - 禁止输出 <plan> 标签（规划阶段已结束）\n\n";
    prompt += "DONE: 完成总结\n";
    prompt += "  - 当前步骤已完成时使用\n\n";
    prompt += "FAIL: 失败原因\n";
    prompt += "  - 步骤无法继续时使用\n\n";
    prompt += "请仅输出你的操作：";

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

void Agent::publishTaskEvent(const std::string& taskId, EventType eventType,
    const std::string& content, const std::string& metadataJson) const {
    json metadata = json::parse(metadataJson, nullptr, false);
    if (metadata.is_discarded()) {
        metadata = json::object();
    }

    // Sprint 2：任务生命周期事件落库到 task_events（周子涵持久化落库）
    persistTaskEvent(taskId, eventType, content, metadata);

    // 推送到 EventBus，供 SSE 实时展示
    if (!ToolSystem::getInstance().isInitialized()) {
        return;
    }
    ToolSystem::getInstance().eventBus().publish(
        EventData::Create(taskId, eventType, content, metadata));
}

void Agent::persistTaskEvent(const std::string& taskId, EventType type,
    const std::string& content, const json& metadata) const {
    if (!db_) {
        return;
    }
    try {
        EventData typeHolder;
        typeHolder.type = type;
        EventRepository(db_).create(
            generateEventId(), taskId, typeHolder.typeToString(),
            content, metadata.dump());
    } catch (const std::exception&) {
        // 落库失败不阻断任务执行
    }
}

void Agent::persistToolCall(const std::string& taskId, const std::string& toolName,
    const json& arguments, const ToolResult& result) const {
    if (!db_) {
        return;
    }
    try {
        ToolCallRepository(db_).create(
            generateToolCallId(), taskId, toolName, arguments.dump(),
            result.success,
            result.success ? result.output : result.error,
            result.exitCode);
    } catch (const std::exception&) {
        // 落库失败不阻断任务执行
    }
}

std::string Agent::generateEventId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    return "event_" + std::to_string(ts) + "_" + std::to_string(++counter);
}

std::string Agent::generateToolCallId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    return "toolcall_" + std::to_string(ts) + "_" + std::to_string(++counter);
}

std::string Agent::normalizeToolName(const std::string& name) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "list") return "file.list";
    if (normalized == "read") return "file.read";
    if (normalized == "write") return "file.write";
    if (normalized == "patch") return "file.apply_patch";
    if (normalized == "shell") return "shell.run";
    if (normalized == "git_status") return "git.status";
    if (normalized == "git_diff") return "git.diff";
    return name;
}

json Agent::buildToolArguments(const std::string& toolName, const std::string& rawArgs) {
    // 1. 空参数
    if (rawArgs.empty()) {
        return json::object();
    }

    // 2. 直接是合法 JSON 对象（<cmd>shell.run {"command":"ls"}</cmd>）
    {
        json parsed = json::parse(rawArgs, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            return parsed;
        }
    }

    // 3. key=value 形式（LLM 常用：<cmd>shell.run command="ls -la /"</cmd>、
    //    <cmd>file.list depth=2</cmd>）。解析为 JSON 对象并做标量类型推断。
    json kv = parseKeyValueArgs(rawArgs);
    if (!kv.empty()) {
        return kv;
    }

    // 4. 裸参数按工具类型包装（<cmd>shell.run ls -la /</cmd>、<cmd>cd workspace</cmd>）
    json args = json::object();
    if (toolName == "shell.run") {
        args["command"] = rawArgs;
    } else if (toolName == "file.read" || toolName == "file.list" ||
               toolName == "cd") {
        args["path"] = rawArgs;
    } else if (toolName == "file.apply_patch") {
        args["patch"] = rawArgs;
    } else {
        args["input"] = rawArgs;
    }
    return args;
}

// 解析 "key=value key2=\"value with spaces\"" 形式的参数为 JSON 对象。
// 若首个 token 不是 key= 形式，返回空对象表示"非 key=value 格式"。
json Agent::parseKeyValueArgs(const std::string& raw) {
    json obj = json::object();
    const std::size_t n = raw.size();
    std::size_t i = 0;
    const auto isKeyChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
               c == '.';
    };

    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(raw[i]))) {
            i++;
        }
        if (i >= n) {
            break;
        }

        // 读取 key
        const std::size_t keyStart = i;
        while (i < n && isKeyChar(raw[i])) {
            i++;
        }
        if (i == keyStart || i >= n || raw[i] != '=') {
            // 不是 key=value 结构，判定为非该格式
            return json::object();
        }
        const std::string key = raw.substr(keyStart, i - keyStart);
        i++; // 跳过 '='

        std::string value;
        if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
            // 引号包裹的值
            const char quote = raw[i++];
            const std::size_t valStart = i;
            while (i < n && raw[i] != quote) {
                i++;
            }
            value = raw.substr(valStart, i - valStart);
            if (i < n) {
                i++; // 跳过收尾引号
            }
        } else {
            // 未加引号：值延伸到下一个 " key=" token 或字符串结尾
            const std::size_t valStart = i;
            std::size_t valEnd = n;
            std::size_t scan = i;
            while (scan < n) {
                if (std::isspace(static_cast<unsigned char>(raw[scan]))) {
                    std::size_t j = scan;
                    while (j < n &&
                           std::isspace(static_cast<unsigned char>(raw[j]))) {
                        j++;
                    }
                    const std::size_t kStart = j;
                    while (j < n && isKeyChar(raw[j])) {
                        j++;
                    }
                    if (j > kStart && j < n && raw[j] == '=') {
                        valEnd = scan;
                        break;
                    }
                }
                scan++;
            }
            i = valEnd;
            value = raw.substr(valStart, valEnd - valStart);
        }

        obj[key] = coerceScalar(value);
    }

    return obj;
}

// 标量类型推断：纯整数 → 整数，true/false → 布尔，其余 → 字符串。
json Agent::coerceScalar(const std::string& value) {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    if (!value.empty()) {
        const std::size_t start = (value[0] == '-') ? 1 : 0;
        bool numeric = start < value.size();
        for (std::size_t k = start; k < value.size(); ++k) {
            if (std::isdigit(static_cast<unsigned char>(value[k])) == 0) {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            try {
                return json(static_cast<std::int64_t>(std::stoll(value)));
            } catch (const std::exception&) {
                // 溢出等异常时退回字符串
            }
        }
    }
    return json(value);
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

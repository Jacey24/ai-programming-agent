#include "Agent.h"
#include "ContextBuilder.h"
#include "LlmProvider.h"
#include "Planner.h"
#include "PromptAdapter.h"
#include "ResponseParser.h"

#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"
#include "facade/SSEGateway.h"

#include "application/ToolSystem.h"
#include "domain/tools/Tool.h"
#include "event/EventBus.h"
#include "infrastructure/filesystem/WorkspaceManager.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <nlohmann/json.hpp>
#include <sstream>

namespace codepilot {

namespace {
bool shouldSkipPlanner(const std::string &goal) {
  const std::vector<std::string> complexSignals = {
      "重构",     "架构",     "多模块",       "依赖",
      "测试失败", "refactor", "architecture", "dependency"};
  for (const auto &signal : complexSignals)
    if (goal.find(signal) != std::string::npos)
      return false;
  return true;
}

// 为可见工具提供简明的参数用法提示，帮助模型正确构造 <cmd> 调用。
std::string describeToolUsage(const std::string &tool) {
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

Agent::Agent(RoleRegistry &registry, Planner &planner)
    : registry_(registry), planner_(planner) {}

AgentResult Agent::executeTask(const std::string &taskId,
                               const std::string &sessionId,
                               const std::string &workspaceId,
                               const std::string &goal) {
  const std::string now = iso8601Now();
  context_.clear();
  prevCommands_.clear();
  deadlockCount_ = 0;

  // ============================================================
  // 阶段 1：规划 (Planner)
  // ============================================================
  publishTaskEvent(taskId, EventType::TaskPlanning,
                   "正在分析任务并选择执行方式…",
                   R"({"stage":"planning","status":"running"})");

  std::vector<PlanStep> steps;
  if (LlmClientFacade::getInstance().isAvailable() &&
      !shouldSkipPlanner(goal)) {
    const std::string planningPrompt =
        planner_.buildPlanningPrompt(goal, toolsDesc_);
    LlmResponse planningResponse =
        LlmClientFacade::getInstance().chat(planningPrompt);
    if (planningResponse.success) {
      steps = Planner::parsePlanFromResponse(planningResponse.content);
      context_.push_back("[planning_response] " + planningResponse.content);
    } else {
      context_.push_back("[planning_fallback] " + planningResponse.error);
    }
  }
  if (steps.empty()) {
    steps = shouldSkipPlanner(goal)
                ? std::vector<PlanStep>{{"executor",
                                         "完成用户要求并进行最小验证", false}}
                : planner_.generatePlan(goal);
  }
  queue_.loadFromPlan(steps);
  json planMetadata{
      {"stage", "planning"}, {"status", "completed"}, {"steps", json::array()}};
  for (const auto &step : steps)
    planMetadata["steps"].push_back(step.action);
  publishTaskEvent(taskId, EventType::AgentMessage,
                   "已生成 " + std::to_string(steps.size()) + " 个执行步骤",
                   planMetadata.dump());

  context_.push_back("[plan] 已生成计划: " + planToJson(steps));
  publishDebugLog(taskId, "已生成计划: " + planToJson(steps), "planner",
                  "planning");

  // 记录规划到 execution_logs（通过 SSEGateway 统一入口）
  SSEGateway::getInstance().push(
      taskId, EventType::AgentMessage, "已生成计划: " + planToJson(steps),
      json{{"source", "planner"}, {"stage", "planning"}},
      SSEGateway::Channel::Debug, SSEGateway::Persist::Always);

  // ============================================================
  // 阶段 2：步骤执行 (Executor)
  // ============================================================
  int loopIterations = 0;
  const int maxIterations = config_.maxSteps > 0 ? config_.maxSteps : 6;
  const int maxRoundsPerStep =
      config_.maxRoundsPerStep > 0 ? config_.maxRoundsPerStep : 3;
  std::string currentStepAction;
  int stepRounds = 0;
  std::string lastResponse;
  while (queue_.hasNext() && loopIterations < maxIterations) {
    ++loopIterations;
    const PlanStep step = queue_.current();
    publishTaskEvent(taskId, EventType::AgentMessage,
                     "正在执行：" + step.action,
                     json{{"stage", "executing"},
                          {"step_index", loopIterations},
                          {"step_total", steps.size()}}
                         .dump());
    if (step.action != currentStepAction) {
      currentStepAction = step.action;
      stepRounds = 0;
    }
    ++stepRounds;
    const RoleConfig *role = registry_.findByName(step.role);
    if (!role) {
      context_.push_back("[error] role not registered: " + step.role);
      queue_.markComplete();
      continue;
    }

    // 2a. 构建本步上下文
    std::string roundCtx = ContextBuilder::buildRound(
        context_, *role, step.action, role->visibleTools);
    context_.push_back("[round] " + roundCtx);
    publishDebugLog(taskId, roundCtx, "executor", "context");

    // 2b. 构建执行期 Prompt
    std::string prompt = buildExecutorPrompt(step, *role, goal);
    context_.push_back("[prompt] " + prompt);
    publishDebugLog(taskId, prompt, "prompt_builder", "prompt");

    // 2c. LLM 调用（通过 LlmClientFacade）
    std::string rawLlmOutput;
    ParsedResponse parsed = executeSingleStep(
        step, *role, goal, taskId, sessionId, workspaceId, rawLlmOutput);
    context_.push_back("[response] " + rawLlmOutput);
    publishDebugLog(taskId, rawLlmOutput, "llm", "response");
    // LLM 自然语言回复推送到 dialog 频道
    if (!rawLlmOutput.empty() && rawLlmOutput.rfind("DONE:", 0) != 0 &&
        rawLlmOutput.rfind("FAIL:", 0) != 0 &&
        SSEGateway::getInstance().isInitialized()) {
      SSEGateway::getInstance().pushDialog(taskId, rawLlmOutput);
    }

    if (!rawLlmOutput.empty() && rawLlmOutput.rfind("DONE:", 0) != 0 &&
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
      context_.push_back("[final] " + parsed.content);
      SSEGateway::getInstance().push(
          taskId, EventType::AgentMessage, "步骤完成: " + step.action,
          json{{"source", "executor"}, {"stage", "step_done"}},
          SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
      queue_.markComplete();
      continue;
    }

    if (parsed.type == ResponseType::ToolCall) {
      bool anyToolFailed = false;

      for (const auto &cmd : parsed.commands) {
        const std::string toolName = normalizeToolName(cmd.toolName);
        const std::string toolCallMsg =
            "[tool] calling: " + toolName +
            (cmd.toolArgs.empty() ? "" : " with args: " + cmd.toolArgs);
        context_.push_back(toolCallMsg);
        publishDebugLog(taskId, toolCallMsg, "tool", "calling");

        ToolContext toolCtx;
        toolCtx.taskId = taskId;
        toolCtx.sessionId = sessionId;
        toolCtx.workspaceId = workspaceId;
        if (!workspaceId.empty()) {
          auto runtime = WorkspaceManager::getInstance().get(workspaceId);
          if (!runtime) {
            std::string workspacePath;
            if (DataAccessFacade::getInstance().isInitialized()) {
              auto workspace =
                  DataAccessFacade::getInstance().getWorkspace(workspaceId);
              if (workspace) {
                workspacePath = workspace->path;
              }
            }
            if (!workspacePath.empty()) {
              runtime = WorkspaceManager::getInstance().getOrCreate(
                  workspaceId, workspacePath);
            }
          }
          if (runtime) {
            toolCtx.workspaceRuntime = runtime;
            toolCtx.workspacePath = runtime->workspacePath;
          }
        }

        json toolArgs = buildToolArguments(toolName, cmd.toolArgs);
        const std::string toolCallId = generateToolCallId();
        toolCtx.options["tool_call_id"] = toolCallId;
        toolCtx.options["require_file_write_permission"] =
            config_.requireFileWritePermission ? "true" : "false";
        toolCtx.options["auto_run_safe_commands"] =
            config_.autoRunSafeCommands ? "true" : "false";
        publishTaskEvent(taskId, EventType::AgentMessage,
                         "准备调用 " + toolName,
                         json{{"stage", "tool"},
                              {"tool_name", toolName},
                              {"tool_call_id", toolCallId}}
                             .dump());

        ToolResult toolResult;
        try {
          if (ToolSystem::getInstance().isInitialized()) {
            toolResult = ToolSystem::getInstance().callToolWithPermission(
                toolName, toolCtx, toolArgs);
          } else {
            toolResult.success = true;
            toolResult.output = "[mock] tool not initialized, " + toolName;
          }
        } catch (const std::exception &e) {
          toolResult.success = false;
          toolResult.error = std::string("Tool call exception: ") + e.what();
        }

        std::ostringstream resultCtx;
        resultCtx << "[tool_result] " << toolName << ": ";
        if (toolResult.success) {
          resultCtx << "success\n" << toolResult.output;
        } else {
          resultCtx << "failed\n" << toolResult.error;
          anyToolFailed = true;
        }
        context_.push_back(resultCtx.str());
        publishDebugLog(taskId, resultCtx.str(), "tool", "result");

        // 写入工具调用日志（通过 SSEGateway 统一入口）
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage, resultCtx.str(),
            json{{"source", "tool"},
                 {"stage", toolResult.success ? "tool_result" : "tool_error"}},
            SSEGateway::Channel::Debug, SSEGateway::Persist::Always);

        // 工具调用落库（通过 DataAccessFacade）
        persistToolCall(taskId, toolName, toolArgs, toolResult);

        if (toolResult.success && isFileMutatingTool(toolName)) {
          persistAndPublishFileChange(taskId, toolName, toolArgs);
        }
      }

      if (stepRounds >= maxRoundsPerStep) {
        context_.push_back("[step_forced_done] 步骤 '" + step.action +
                           "' 已达单步工具轮次上限，自动完成并进入下一步");
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage,
            "步骤达到轮次上限自动完成: " + step.action,
            json{{"source", "executor"}, {"stage", "step_forced_done"}},
            SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
        queue_.markComplete();
      }
      continue;
    }

    if (parsed.type == ResponseType::Plan) {
      context_.push_back("[plan_update] received new plan from LLM");
      for (const auto &newStep : parsed.planSteps) {
        queue_.insertAfterCurrent(newStep);
      }
      queue_.markComplete();
      continue;
    }

    if (parsed.isFail) {
      context_.push_back("[fail] " + parsed.failReason);
      SSEGateway::getInstance().push(
          taskId, EventType::AgentMessage, parsed.failReason,
          json{{"source", "executor"}, {"stage", "step_failed"}},
          SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
      queue_.markFailed();
      continue;
    }

    queue_.markComplete();
  }

  // ============================================================
  // 阶段 3：汇总 (Summarizer)
  // ============================================================
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
  const std::string finalContent =
      result.status == "completed"
          ? (lastResponse.empty() ? std::string("Agent task completed")
                                  : lastResponse)
          : std::string("Agent task failed");
  if (result.status == "completed") {
    const ParsedResponse finalResponse = ResponseParser::parseAll(finalContent);
    publishTaskEvent(taskId, EventType::AgentMessage,
                     finalResponse.content.empty() ? "代码已经生成并完成验证。"
                                                   : finalResponse.content);
  }
  publishTaskEvent(taskId,
                   result.status == "completed" ? EventType::TaskCompleted
                                                : EventType::TaskFailed,
                   result.status == "completed" ? "任务完成" : finalContent,
                   "{\"status\":\"" + escapeJson(result.status) + "\"}");
  return result;
}

ParsedResponse Agent::executeSingleStep(
    const PlanStep &step, const RoleConfig &role, const std::string &goal,
    const std::string &taskId, const std::string &sessionId,
    const std::string &workspaceId, std::string &rawLlmOutput) {
  std::string prompt = buildExecutorPrompt(step, role, goal);

  // 通过 LlmClientFacade 调用 LLM（自动选择 provider）
  if (LlmClientFacade::getInstance().isAvailable()) {
    LlmResponse response = LlmClientFacade::getInstance().chat(prompt);
    if (response.success && !response.content.empty()) {
      rawLlmOutput = response.content;
      return ResponseParser::parseAll(rawLlmOutput);
    }
    const std::string reason =
        response.error.empty() ? "LLM 未返回内容" : response.error;
    context_.push_back("[llm_fallback] " + reason);
    rawLlmOutput = "FAIL: LLM 调用失败: " + reason;
    return ResponseParser::parseAll(rawLlmOutput);
  }

  // 未配置 LLM（纯 mock/测试模式）：返回确定性完成结果
  rawLlmOutput = "DONE: step '" + step.action + "' completed by mock fallback";
  return ResponseParser::parseAll(rawLlmOutput);
}

std::string Agent::buildExecutorPrompt(const PlanStep &step,
                                       const RoleConfig &role,
                                       const std::string &goal) const {
  std::string prompt;

  prompt += "[" + role.name + "] " + role.description + "\n\n";

  prompt += "## 系统信息\n";
  prompt += "当前工作区: workspace（所有相对路径基于工作区根目录）\n\n";

  prompt += "## 已完成的操作与结果\n";
  {
    std::vector<std::string> relevant;
    for (const auto &entry : context_) {
      if (entry.rfind("[tool_result]", 0) == 0 ||
          entry.rfind("[tool]", 0) == 0 || entry.rfind("[final]", 0) == 0 ||
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

  prompt += "## 用户原始需求\n";
  prompt += goal + "\n\n";

  prompt += "## 编程任务成果交付规则\n";
  prompt += "1. 用户要求编写、生成或实现代码时，必须将代码写入 workspace "
            "中的真实文件。\n";
  prompt += "2. 用户未指定文件名时，根据语言和任务生成合理文件名。\n";
  prompt += "3. 不允许只在 DONE 中输出代码而不创建或修改文件。\n";
  prompt += "4. 写入文件后，使用 file.read 检查文件内容。\n";
  prompt += "5. 可以运行时，使用 shell.run 执行最小验证。\n";
  prompt += "6. 文件成功创建并验证后，立即返回 DONE，不要重复 file.list 或 "
            "file.read。\n";
  prompt += "7. 同一目录最多探索一次；确认目标文件不存在后应直接创建。\n\n";

  prompt += "## 当前步骤\n";
  prompt += step.action + "\n\n";

  prompt += "## 可用工具（只能使用以下工具）\n";
  if (!role.visibleTools.empty()) {
    for (const auto &t : role.visibleTools) {
      prompt += "  - " + t + describeToolUsage(t) + "\n";
    }
  } else {
    prompt += "（无特殊工具限制）\n";
  }
  prompt += "\n";

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

bool Agent::isDeadlock(const std::vector<ParsedCommand> &commands) const {
  if (prevCommands_.empty() || commands.empty())
    return false;
  if (commands.size() != prevCommands_.size())
    return false;

  for (size_t i = 0; i < commands.size(); ++i) {
    if (commands[i].toolName != prevCommands_[i].toolName)
      return false;
    if (commands[i].toolArgs != prevCommands_[i].toolArgs)
      return false;
  }
  return true;
}

void Agent::publishTaskEvent(const std::string &taskId, EventType eventType,
                             const std::string &content,
                             const std::string &metadataJson) const {
  json metadata = json::parse(metadataJson, nullptr, false);
  if (metadata.is_discarded()) {
    metadata = json::object();
  }

  // 通过 SSEGateway 发布事件（自动完成 EventBus 推送 + SQLite 落库）
  if (SSEGateway::getInstance().isInitialized()) {
    SSEGateway::getInstance().pushStatus(taskId, content, eventType,
                                         metadata.dump());
    return;
  }

  // Fallback：SSEGateway 未初始化时，走旧路径
  EventData event = EventData::Create(taskId, eventType, content, metadata);

  if (DataAccessFacade::getInstance().isInitialized()) {
    DataAccessFacade::getInstance().saveEvent(
        event.id, event.taskId, event.typeToString(), event.content,
        event.metadata.dump());
  }

  if (ToolSystem::getInstance().isInitialized()) {
    ToolSystem::getInstance().eventBus().publish(event);
  }
}

AgentResult Agent::executeDirectAnswer(const std::string &taskId,
                                       const std::string &sessionId,
                                       const std::string &workspaceId,
                                       const std::string &goal) {
  const std::string now = iso8601Now();
  publishTaskEvent(
      taskId, EventType::TaskPlanning, "已识别为直接回答任务。",
      R"({"stage":"planning","status":"completed","mode":"answer"})");
  const std::string prompt =
      "你是编程助手。\n请直接回答用户问题。\n代码必须使用 Markdown "
      "代码块。\n不要创建文件，不要调用工具，不要输出计划标签或命令标签。\n\n用"
      "户请求：\n" +
      goal;
  std::string answer;
  if (LlmClientFacade::getInstance().isAvailable()) {
    const LlmResponse response = LlmClientFacade::getInstance().chat(prompt);
    answer =
        response.success ? response.content : "无法生成回答：" + response.error;
  } else {
    answer = "当前未配置 LLM，无法生成直接回答。";
  }
  publishTaskEvent(taskId, EventType::AgentMessage, answer,
                   R"({"stage":"answer","status":"completed"})");
  publishTaskEvent(taskId, EventType::TaskCompleted, "任务完成",
                   R"({"status":"completed"})");
  AgentResult result;
  result.taskId = taskId;
  result.sessionId = sessionId;
  result.workspaceId = workspaceId;
  result.goal = goal;
  result.status = "completed";
  result.planJson = "[]";
  result.currentStep = "answered";
  result.createdAt = now;
  result.updatedAt = iso8601Now();
  return result;
}

void Agent::publishDebugLog(const std::string &taskId,
                            const std::string &content,
                            const std::string &source,
                            const std::string &stage) const {
  if (!SSEGateway::getInstance().isInitialized()) {
    return;
  }
  json meta{{"channel", "debug"}, {"source", source}};
  if (!stage.empty()) {
    meta["stage"] = stage;
  }
  SSEGateway::getInstance().pushDebug(taskId, content, EventType::AgentMessage,
                                      meta.dump());
}

void Agent::persistTaskEvent(const EventData &event) const {
  if (!DataAccessFacade::getInstance().isInitialized()) {
    return;
  }
  try {
    DataAccessFacade::getInstance().saveEvent(
        event.id, event.taskId, event.typeToString(), event.content,
        event.metadata.dump());
  } catch (const std::exception &) {
    // 落库失败不阻断任务执行
  }
}

void Agent::persistToolCall(const std::string &taskId,
                            const std::string &toolName, const json &arguments,
                            const ToolResult &result) const {
  if (!DataAccessFacade::getInstance().isInitialized()) {
    return;
  }
  try {
    DataAccessFacade::getInstance().saveToolCall(
        generateToolCallId(), taskId, toolName, arguments.dump(),
        result.success, result.success ? result.output : result.error,
        result.exitCode);
  } catch (const std::exception &) {
    // 落库失败不阻断任务执行
  }
}

void Agent::persistAndPublishFileChange(const std::string &taskId,
                                        const std::string &toolName,
                                        const json &arguments) const {
  const std::string path = extractChangedPath(toolName, arguments);
  if (path.empty()) {
    return;
  }

  const std::string changeType = inferChangeType(toolName);
  if (DataAccessFacade::getInstance().isInitialized()) {
    try {
      DataAccessFacade::getInstance().recordFileChange(
          generateFileChangeId(), taskId, path, changeType, "");
    } catch (const std::exception &) {
      // File change persistence should not block the tool result.
    }
  }

  publishTaskEvent(
      taskId, EventType::FileChanged, "File changed: " + path,
      json{{"path", path}, {"change_type", changeType}, {"tool_name", toolName}}
          .dump());
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

std::string Agent::generateFileChangeId() {
  static std::atomic<std::uint64_t> counter{0};
  const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
  return "filechange_" + std::to_string(ts) + "_" + std::to_string(++counter);
}

bool Agent::isFileMutatingTool(const std::string &toolName) {
  return toolName == "file.write" || toolName == "file.apply_patch" ||
         toolName == "file.mkdir" || toolName == "file.remove" ||
         toolName == "file.rmdir" || toolName == "file.move" ||
         toolName == "file.copy";
}

std::string Agent::extractChangedPath(const std::string &toolName,
                                      const json &arguments) {
  const auto readString = [&](const char *key) -> std::string {
    if (arguments.contains(key) && arguments.at(key).is_string()) {
      return arguments.at(key).get<std::string>();
    }
    return "";
  };

  if (toolName == "file.apply_patch") {
    const std::string filePath = readString("file_path");
    if (!filePath.empty()) {
      return filePath;
    }
  }
  if (toolName == "file.move" || toolName == "file.copy") {
    const std::string destination = readString("destination");
    if (!destination.empty()) {
      return destination;
    }
    const std::string to = readString("to");
    if (!to.empty()) {
      return to;
    }
  }

  const std::string path = readString("path");
  if (!path.empty()) {
    return path;
  }
  return readString("file_path");
}

std::string Agent::inferChangeType(const std::string &toolName) {
  if (toolName == "file.remove" || toolName == "file.rmdir") {
    return "deleted";
  }
  if (toolName == "file.move") {
    return "moved";
  }
  if (toolName == "file.copy" || toolName == "file.write" ||
      toolName == "file.mkdir") {
    return "created";
  }
  return "modified";
}

std::string Agent::normalizeToolName(const std::string &name) {
  std::string normalized = name;
  std::transform(
      normalized.begin(), normalized.end(), normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (normalized == "list")
    return "file.list";
  if (normalized == "read")
    return "file.read";
  if (normalized == "write")
    return "file.write";
  if (normalized == "patch")
    return "file.apply_patch";
  if (normalized == "shell")
    return "shell.run";
  if (normalized == "git_status")
    return "git.status";
  if (normalized == "git_diff")
    return "git.diff";
  return name;
}

json Agent::buildToolArguments(const std::string &toolName,
                               const std::string &rawArgs) {
  if (rawArgs.empty()) {
    return json::object();
  }

  {
    json parsed = json::parse(rawArgs, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
      return parsed;
    }
  }

  json kv = parseKeyValueArgs(rawArgs);
  if (!kv.empty()) {
    return kv;
  }

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

json Agent::parseKeyValueArgs(const std::string &raw) {
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

    const std::size_t keyStart = i;
    while (i < n && isKeyChar(raw[i])) {
      i++;
    }
    if (i == keyStart || i >= n || raw[i] != '=') {
      return json::object();
    }
    const std::string key = raw.substr(keyStart, i - keyStart);
    i++; // 跳过 '='

    std::string value;
    if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
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
      const std::size_t valStart = i;
      std::size_t valEnd = n;
      std::size_t scan = i;
      while (scan < n) {
        if (std::isspace(static_cast<unsigned char>(raw[scan]))) {
          std::size_t j = scan;
          while (j < n && std::isspace(static_cast<unsigned char>(raw[j]))) {
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

json Agent::coerceScalar(const std::string &value) {
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
      } catch (const std::exception &) {
        // 溢出等异常时退回字符串
      }
    }
  }
  return json(value);
}

std::string Agent::toolsToString(const std::vector<std::string> &tools) {
  std::string s;
  for (size_t i = 0; i < tools.size(); ++i) {
    if (i > 0)
      s += ", ";
    s += tools[i];
  }
  return s;
}

std::string Agent::planToJson(const std::vector<PlanStep> &steps) {
  std::string json = "[";
  for (size_t i = 0; i < steps.size(); ++i) {
    if (i > 0)
      json += ",";
    json += "\"" + escapeJson(steps[i].action) + "\"";
  }
  json += "]";
  return json;
}

std::string Agent::escapeJson(const std::string &s) {
  std::string out;
  for (char ch : s) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out += ch;
    }
  }
  return out;
}

std::string Agent::iso8601Now() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm *tm = std::gmtime(&t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return buf;
}

} // namespace codepilot

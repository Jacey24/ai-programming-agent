#include "AgentLoop.h"
#include "MessageBus.h"
#include "PromptBuilder.h"
#include "RouteEngine.h"

#include "application/ToolSystem.h"
#include "common/logging/Logger.h"
#include "domain/security/PermissionPolicyEngine.h"
#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"
#include "facade/SSEGateway.h"
#include "infrastructure/filesystem/WorkspaceManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>

namespace codepilot {

namespace {

constexpr int kMaxLlmAttemptsPerExpert = 2;

bool hasReadableText(const std::string &value) {
  return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

std::string safeLlmError(const LlmResponse &response) {
  switch (response.errorKind) {
  case LlmErrorKind::NotConfigured:
    return "LLM 未配置";
  case LlmErrorKind::Http:
    return response.httpStatus > 0
               ? "LLM 服务返回 HTTP " + std::to_string(response.httpStatus)
               : "LLM 服务返回错误响应";
  case LlmErrorKind::EmptyResponse:
    return "LLM 返回空响应";
  case LlmErrorKind::InvalidResponse:
    return "LLM 返回无法解析的响应";
  case LlmErrorKind::Transport:
    return "LLM 连接或请求超时";
  case LlmErrorKind::None:
  default:
    return "LLM 调用失败";
  }
}

} // namespace

AgentLoop::AgentLoop(const std::string &configPath) : configPath_(configPath) {
  AgentConfiguration::getInstance().init(configPath);
}

bool AgentLoop::isReady() const {
  return AgentConfiguration::getInstance().isReady();
}

AgentLoopResult
AgentLoop::runFromSnapshot(const TaskSnapshot &snapshot,
                           std::shared_ptr<std::atomic<bool>> cancelFlag) {
  AgentLoopResult result;
  auto &cfg = AgentConfiguration::getInstance();

  if (!cfg.isReady()) {
    result.status = "config_error";
    result.finalOutput = "[runFromSnapshot] Expert 配置未加载或加载失败";
    return result;
  }

  const ExpertConfig *targetExpert = cfg.getExpert(snapshot.currentExpert);
  if (!targetExpert) {
    result.status = "config_error";
    result.finalOutput = "[runFromSnapshot] 快照中的 Expert [" +
                         snapshot.currentExpert + "] 不在当前配置中";
    return result;
  }

  TaskContext ctx;
  ctx.taskId = snapshot.taskId;
  ctx.globalId = snapshot.globalId;
  ctx.workspaceId = snapshot.workspaceId;
  ctx.goal = snapshot.goal;
  ctx.workspacePath = snapshot.workspacePath;
  ctx.summary = snapshot.summary.empty() ? "" : snapshot.summary;
  ctx.currentPlan = snapshot.currentPlan;
  // planHistory is restored via PlanManager::restoreSnapshot inside
  // runExpertChain

  // 从快照恢复时使用默认选项
  TaskRunOptions defaultOpts;
  return runExpertChain(snapshot.taskId, snapshot.globalId,
                        snapshot.workspaceId, ctx, targetExpert, defaultOpts,
                        cancelFlag, snapshot.sessionHistory);
}

AgentLoopResult AgentLoop::run(const std::string &taskId,
                               const std::string &globalId,
                               const std::string &workspaceId,
                               const std::string &goal,
                               const TaskRunOptions &options,
                               std::shared_ptr<std::atomic<bool>> cancelFlag) {
  auto &cfg = AgentConfiguration::getInstance();
  cfg.reconfigure();

  AgentLoopResult result;
  if (!cfg.isReady()) {
    result.status = "config_error";
    result.finalOutput = "Expert 配置未加载或加载失败";
    return result;
  }

  const ExpertConfig *entryExpert = cfg.getEntryExpert();
  if (!entryExpert) {
    result.status = "config_error";
    result.finalOutput = "未找到入口 Expert（is_entry=true）";
    return result;
  }

  TaskContext ctx;
  ctx.taskId = taskId;
  ctx.globalId = globalId;
  ctx.workspaceId = workspaceId;
  ctx.goal = goal;

  // 解析 workspace 路径：优先从 WorkspaceManager 缓存获取，降级查
  // DB，最后用默认值
  {
    auto &wm = WorkspaceManager::getInstance();
    auto rt = wm.get(workspaceId);
    if (!rt && DataAccessFacade::getInstance().isInitialized()) {
      auto ws = DataAccessFacade::getInstance().getWorkspace(workspaceId);
      if (ws) {
        rt = wm.getOrCreate(workspaceId, ws->path);
      }
    }
    if (!rt) {
      result.status = "workspace_error";
      result.finalOutput = "Workspace runtime is unavailable: " + workspaceId;
      return result;
    }
    ctx.workspacePath = rt->workspacePath;
  }

  // ★ v2: 从 global_context 加载历史摘要注入首轮 prompt
  // 让同一 Global 下的后续 Task 自动感知前序任务的上下文
  if (DataAccessFacade::getInstance().isInitialized()) {
    auto contexts = DataAccessFacade::getInstance().getGlobalContext(globalId);
    for (const auto &c : contexts) {
      if (c.type == "summary") {
        ctx.summary = "## 历史任务摘要\n" + c.content + "\n";
        break; // 只取最近一条 summary
      }
    }
  }

  return runExpertChain(taskId, globalId, workspaceId, ctx, entryExpert,
                        options, cancelFlag);
}

AgentLoopResult AgentLoop::runExpertChain(
    const std::string &taskId, const std::string &globalId,
    const std::string &workspaceId, TaskContext &ctx,
    const ExpertConfig *entryExpert, const TaskRunOptions &options,
    std::shared_ptr<std::atomic<bool>> cancelFlag,
    const std::string &initialSessionHistory) {
  AgentLoopResult result;
  auto &cfg = AgentConfiguration::getInstance();

  PlanManager planMgr;

  const auto cancelledResult = [&]() {
    AgentLoopResult cancelled;
    cancelled.status = "cancelled";
    cancelled.finalOutput = "任务已被用户取消";
    cancelled.finalPlan = planMgr.snapshot();
    cancelled.summary = ctx.summary;
    return cancelled;
  };

  const ExpertConfig *currentExpert = entryExpert;
  std::string sessionHistory = initialSessionHistory;

  const int effectiveMaxSteps = std::clamp(options.maxSteps, 1, 20);
  int expertSwitches = 0;
  bool firstRoundInExpert = true;
  int roundsLeft = 0; // ★ 提升到外层，供 double_check continue 使用

  while (currentExpert && expertSwitches < effectiveMaxSteps) {
    if (cancelFlag && cancelFlag->load()) {
      result.status = "cancelled";
      result.finalOutput = "任务已被用户取消";
      result.finalPlan = planMgr.snapshot();
      result.summary = ctx.summary;
      return result;
    }

    ++expertSwitches;
    result.expertChain.push_back(currentExpert->name);

    ctx.currentPlan = planMgr.snapshot();
    ctx.planHistory = planMgr.history();

    if (SSEGateway::getInstance().isInitialized()) {
      std::string stageMsg;
      stageMsg = "Expert [" + currentExpert->name + "] 开始工作";
      json meta;
      meta["expert"] = currentExpert->name;
      meta["stage"] = "expert_start";
      SSEGateway::getInstance().push(taskId, EventType::AgentMessage, stageMsg,
                                     meta, SSEGateway::Channel::Status,
                                     SSEGateway::Persist::Always);
    }

    if (currentExpert->contextIsolation) {
      sessionHistory.clear();
      firstRoundInExpert = true;
    }

    // ★ 仅在正常进入 Expert（非 double_check 重试）时初始化 roundsLeft
    if (firstRoundInExpert) {
      roundsLeft = currentExpert->maxInternalRounds;
    }
    int globalReadsUsed = 0;
    int llmAttempts = 0;

    // 兜底保护：executor 进入时如果既没有历史也没有有效计划，直接终止
    if (currentExpert->name == "executor" && sessionHistory.empty() &&
        ctx.currentPlan.steps.empty() && !currentExpert->isEntry) {
      return finalizeWithCriticalSummary(
          taskId,
          "Expert [executor] 没有收到有效计划或上下文，"
          "可能是 planner 未生成步骤。请重试任务。",
          "failed", sessionHistory, planMgr, ctx);
    }

    std::string lastOutput;
    std::string prompt; // ★ 声明到外层，供 double_check 复用

    while (roundsLeft > 0) {
      if (cancelFlag && cancelFlag->load()) {
        result.status = "cancelled";
        result.finalOutput = "任务已被用户取消";
        result.finalPlan = planMgr.snapshot();
        result.summary = ctx.summary;
        return result;
      }

      if (sessionHistory.empty()) {
        prompt = PromptBuilder::buildInitial(*currentExpert, ctx);
      } else {
        prompt = PromptBuilder::buildNextRound(
            *currentExpert, ctx, sessionHistory, lastOutput, roundsLeft);
      }
      firstRoundInExpert = false;

      if (SSEGateway::getInstance().isInitialized()) {
        json debugMeta;
        debugMeta["source"] = "expert_loop";
        debugMeta["expert"] = currentExpert->name;
        debugMeta["rounds_left"] = roundsLeft;
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage,
            "[" + currentExpert->name + "] 第" +
                std::to_string(currentExpert->maxInternalRounds - roundsLeft +
                               1) +
                "轮, 剩余" + std::to_string(roundsLeft) + "轮",
            debugMeta, SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
      }

      LlmResponse llmResp;

      if (SSEGateway::getInstance().isInitialized()) {
        json waitMeta;
        waitMeta["source"] = "llm";
        waitMeta["expert"] = currentExpert->name;
        waitMeta["stage"] = "waiting";
        std::string providerHint;
        if (!currentExpert->llmProvider.empty())
          providerHint = " via " + currentExpert->llmProvider;
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage,
            "[" + currentExpert->name + "] 正在等待 LLM 响应…" + providerHint,
            waitMeta, SSEGateway::Channel::Status, SSEGateway::Persist::Always);
      }

      if (LlmClientFacade::getInstance().isAvailable()) {
        std::string provider = currentExpert->llmProvider.empty()
                                   ? ""
                                   : currentExpert->llmProvider;
        std::string model =
            currentExpert->llmModel.empty() ? "" : currentExpert->llmModel;
        int timeout =
            currentExpert->llmTimeout > 0 ? currentExpert->llmTimeout : 60;

        ++llmAttempts;
        llmResp = LlmClientFacade::getInstance().chat(prompt, provider, model,
                                                      timeout);
      } else {
        return finalizeWithCriticalSummary(
            taskId, "LLM 未配置，无法执行任务", "failed", sessionHistory,
            planMgr, ctx);
      }

      if (cancelFlag && cancelFlag->load()) {
        return cancelledResult();
      }

      if (!llmResp.success || !hasReadableText(llmResp.content)) {
        const std::string safeError = safeLlmError(llmResp);
        sessionHistory += "\n[system] " + safeError + "\n";
        if (SSEGateway::getInstance().isInitialized()) {
          json errMeta;
          errMeta["source"] = "llm";
          errMeta["expert"] = currentExpert->name;
          errMeta["stage"] = "error";
          SSEGateway::getInstance().push(
              taskId, EventType::AgentMessage,
              "[" + currentExpert->name + "] " + safeError,
              errMeta, SSEGateway::Channel::Status,
              SSEGateway::Persist::Always);
        }
        if (llmAttempts >= kMaxLlmAttemptsPerExpert) {
          return finalizeWithCriticalSummary(
              taskId,
              "Expert [" + currentExpert->name + "] " + safeError +
                  "，已达到 2 次尝试上限",
              "failed", sessionHistory, planMgr, ctx);
        }
        roundsLeft--;
        continue;
      }

      if (SSEGateway::getInstance().isInitialized()) {
        json recvMeta;
        recvMeta["source"] = "llm";
        recvMeta["expert"] = currentExpert->name;
        recvMeta["stage"] = "received";
        recvMeta["size"] = static_cast<int>(llmResp.content.size());
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage,
            "[" + currentExpert->name + "] LLM 响应就绪 (" +
                std::to_string(llmResp.content.size()) + " 字符)",
            recvMeta, SSEGateway::Channel::Status, SSEGateway::Persist::Always);
      }

      lastOutput = llmResp.content;

      std::string dialogContent;
      {
        bool inTag = false;
        for (size_t i = 0; i < lastOutput.size(); ++i) {
          if (lastOutput[i] == '<')
            inTag = true;
          else if (lastOutput[i] == '>')
            inTag = false;
          else if (!inTag)
            dialogContent += lastOutput[i];
        }
        if (SSEGateway::getInstance().isInitialized()) {
          json rawMeta;
          rawMeta["source"] = "llm_raw";
          rawMeta["expert"] = currentExpert->name;
          SSEGateway::getInstance().push(
              taskId, EventType::AgentMessage, lastOutput, rawMeta,
              SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
        }
        if (DataAccessFacade::getInstance().isInitialized()) {
          DataAccessFacade::getInstance().appendLog(taskId, "llm_raw",
                                                    lastOutput);
        }
      }

      TagCollection tags = MessageBus::parse(lastOutput);

      // A readable summarizer result is valuable even if its control tag is
      // malformed. Preserve it as the sole final output, but keep the task
      // failed because the configured protocol was not satisfied.
      if (currentExpert->name == "summarizer" && !tags.has("done") &&
          !tags.has("fail") && hasReadableText(dialogContent)) {
        result.status = "failed";
        result.finalOutput = dialogContent;
        result.finalPlan = planMgr.snapshot();
        result.summary = ctx.summary;
        return result;
      }

      // ── <cmd> ──
      int acceptedCmdCount = 0;
      for (const auto &cmd : tags.get("cmd")) {
        if (cancelFlag && cancelFlag->load()) {
          return cancelledResult();
        }
        std::string actualToolName;
        std::string argsStr;

        size_t spacePos = cmd.content.find_first_of(" {");
        if (spacePos != std::string::npos) {
          actualToolName = cmd.content.substr(0, spacePos);
          argsStr = cmd.content.substr(spacePos + 1);
        } else {
          actualToolName = cmd.content;
        }

        if (std::find(currentExpert->visibleTools.begin(),
                      currentExpert->visibleTools.end(),
                      actualToolName) == currentExpert->visibleTools.end()) {
          std::string availableList;
          if (currentExpert->visibleTools.empty()) {
            availableList = "（当前 Expert 未配置可用工具）";
          } else {
            availableList = "可用: ";
            for (size_t ti = 0; ti < currentExpert->visibleTools.size(); ++ti) {
              if (ti > 0)
                availableList += ", ";
              availableList += currentExpert->visibleTools[ti];
            }
          }
          sessionHistory +=
              "\n[system] [NOT_AVAILABLE] 工具 [" + actualToolName +
              "] 不在当前 Expert [" + currentExpert->name +
              "] 的允许列表中，该调用已被拒绝。\n→ " + availableList + "\n";
          continue;
        }

        json toolArgsParsed;
        if (!argsStr.empty()) {
          toolArgsParsed = json::parse(argsStr, nullptr, false);
        }
        if (toolArgsParsed.is_discarded()) {
          toolArgsParsed = json::object();
        }

        if (SSEGateway::getInstance().isInitialized()) {
          json toolMeta;
          toolMeta["stage"] = "tool";
          toolMeta["tool_name"] = actualToolName;
          SSEGateway::getInstance().push(taskId, EventType::AgentMessage,
                                         "准备调用 " + actualToolName, toolMeta,
                                         SSEGateway::Channel::Status,
                                         SSEGateway::Persist::Always);
        }

        std::string toolResult;
        bool toolSuccess = false;
        int toolExitCode = 0;
        if (ToolSystem::getInstance().isInitialized()) {
          ToolContext toolCtx;
          toolCtx.taskId = taskId;
          toolCtx.sessionId = globalId;
          toolCtx.workspaceId = workspaceId;
          toolCtx.workspacePath = ctx.workspacePath;
          if (!workspaceId.empty()) {
            toolCtx.workspaceRuntime =
                WorkspaceManager::getInstance().getOrCreate(
                    workspaceId, toolCtx.workspacePath);
          }
          toolCtx.options["auto_run_safe_commands"] =
              options.autoRunSafeCommands ? "true" : "false";
          toolCtx.options["require_file_write_permission"] =
              options.requireFileWritePermission ? "true" : "false";

          try {
            // ★★★ Workspace 权限策略预检 ★★★
            // 先从 DB 读取当前 workspace 的 permissions_config，
            // 用 PolicyEngine 评估是否自动批准/拒绝，仅当策略为
            // "ask" 时才进入标准 PermissionManager 暂停流程。
            PolicyAction policyAction = PolicyAction::Ask;
            {
              std::string permissionsConfig = "{}";
              if (DataAccessFacade::getInstance().isInitialized()) {
                auto wsRecord =
                    DataAccessFacade::getInstance().getWorkspace(workspaceId);
                if (wsRecord) {
                  permissionsConfig = wsRecord->permissions_config;
                }
              }
              policyAction = PermissionPolicyEngine::evaluate(permissionsConfig,
                                                              actualToolName);
            }

            ToolResult tr;
            if (policyAction == PolicyAction::Deny) {
              // 策略拒绝 → 构造 [PERMISSION_DENIED] 结果
              toolSuccess = false;
              toolExitCode = -1;
              toolResult = "[PERMISSION_DENIED] Workspace 策略拒绝了工具 [" +
                           actualToolName + "] 的执行权限。";

              if (SSEGateway::getInstance().isInitialized()) {
                json policyMeta;
                policyMeta["stage"] = "policy_denied";
                policyMeta["tool_name"] = actualToolName;
                policyMeta["source"] = "PermissionPolicyEngine";
                SSEGateway::getInstance().push(
                    taskId, EventType::AgentMessage,
                    "🚫 策略拒绝: " + actualToolName, policyMeta,
                    SSEGateway::Channel::Status, SSEGateway::Persist::Always);
              }
            } else if (policyAction == PolicyAction::AutoApprove) {
              // 策略自动批准 → 跳过权限检查，直接执行
              if (SSEGateway::getInstance().isInitialized()) {
                json policyMeta;
                policyMeta["stage"] = "policy_auto_approve";
                policyMeta["tool_name"] = actualToolName;
                policyMeta["source"] = "PermissionPolicyEngine";
                SSEGateway::getInstance().push(
                    taskId, EventType::AgentMessage,
                    "⚡ 自动批准执行: " + actualToolName, policyMeta,
                    SSEGateway::Channel::Status, SSEGateway::Persist::Always);
              }

              if (cancelFlag && cancelFlag->load()) {
                return cancelledResult();
              }
              tr = ToolSystem::getInstance().callTool(actualToolName, toolCtx,
                                                      toolArgsParsed);
              toolSuccess = tr.success;
              toolExitCode = tr.exitCode;
              if (tr.success) {
                toolResult = "成功: " + tr.output;
              } else {
                toolResult = "[EXECUTION_ERROR] " + tr.error + " (exit code " +
                             std::to_string(tr.exitCode) + ")";
              }
            } else {
              // Ask → 走标准 PermissionManager 暂停/继续流程
              tr = ToolSystem::getInstance().callToolWithPermission(
                  actualToolName, toolCtx, toolArgsParsed);
            }

            // ★★★ 权限暂停检测（仅 Ask 策略会进入） ★★★
            if (policyAction == PolicyAction::Ask) {
              // 当工具需要用户批准时，callToolWithPermission 返回
              // metadata["paused"]=true 而非阻塞。
              // 此处调用 waitForResolution 阻塞等待用户决议，
              // 批准后重新执行工具，实现无缝继续。
              if (tr.metadata.contains("paused") &&
                  tr.metadata["paused"].get<bool>()) {
                std::string permReqId =
                    tr.metadata.value("permission_request_id", "");

                if (SSEGateway::getInstance().isInitialized()) {
                  json pauseMeta;
                  pauseMeta["stage"] = "permission_wait";
                  pauseMeta["tool_name"] = actualToolName;
                  pauseMeta["request_id"] = permReqId;
                  SSEGateway::getInstance().push(
                      taskId, EventType::AgentMessage,
                      "⏸ 等待用户批准: " + actualToolName +
                          " (request=" + permReqId + ")",
                      pauseMeta, SSEGateway::Channel::Status,
                      SSEGateway::Persist::Always);
                }

                // 阻塞等待用户决议（最多 120 秒）
                bool approved = ToolSystem::getInstance()
                                    .permissionManager()
                                    .waitForResolution(permReqId, 120);

                if (cancelFlag && cancelFlag->load()) {
                  return cancelledResult();
                }

                if (approved) {
                  // 用户批准 → 重新执行工具（跳过权限检查，直接调用 callTool）
                  if (SSEGateway::getInstance().isInitialized()) {
                    json approvedMeta;
                    approvedMeta["stage"] = "permission_approved";
                    approvedMeta["tool_name"] = actualToolName;
                    approvedMeta["request_id"] = permReqId;
                    SSEGateway::getInstance().push(
                        taskId, EventType::AgentMessage,
                        "✅ 权限已批准，执行 " + actualToolName, approvedMeta,
                        SSEGateway::Channel::Status,
                        SSEGateway::Persist::Always);
                  }

                  tr = ToolSystem::getInstance().callTool(
                      actualToolName, toolCtx, toolArgsParsed);
                  toolSuccess = tr.success;
                  toolExitCode = tr.exitCode;
                  if (tr.success) {
                    toolResult = "成功: " + tr.output;
                  } else {
                    toolResult = "[EXECUTION_ERROR] " + tr.error +
                                 " (exit code " + std::to_string(tr.exitCode) +
                                 ")";
                  }
                } else {
                  // 用户拒绝或超时
                  toolSuccess = false;
                  toolExitCode = -1;
                  toolResult = "[PERMISSION_DENIED] 用户拒绝了工具 [" +
                               actualToolName + "] 的执行权限，或等待超时。";

                  if (SSEGateway::getInstance().isInitialized()) {
                    json deniedMeta;
                    deniedMeta["stage"] = "permission_denied";
                    deniedMeta["tool_name"] = actualToolName;
                    deniedMeta["request_id"] = permReqId;
                    SSEGateway::getInstance().push(
                        taskId, EventType::AgentMessage,
                        "❌ 权限被拒绝: " + actualToolName, deniedMeta,
                        SSEGateway::Channel::Status,
                        SSEGateway::Persist::Always);
                  }
                }
              } else {
                // 无需权限暂停，正常处理结果
                toolSuccess = tr.success;
                toolExitCode = tr.exitCode;
                if (tr.success) {
                  toolResult = "成功: " + tr.output;
                } else {
                  toolResult = "[EXECUTION_ERROR] " + tr.error +
                               " (exit code " + std::to_string(tr.exitCode) +
                               ")";
                }
              }
            } // end of Ask policy block
          } catch (const std::exception &e) {
            toolSuccess = false;
            toolResult = std::string("异常: ") + e.what();
          }
        } else {
          toolResult =
              "[mock] " + actualToolName + " 执行成功（ToolSystem 未初始化）";
        }

        sessionHistory +=
            "\n[system] 工具 [" + actualToolName + "]: " + toolResult + "\n";
        acceptedCmdCount++;

        if (DataAccessFacade::getInstance().isInitialized()) {
          DataAccessFacade::getInstance().appendLog(
              taskId, "tool_call", "[" + actualToolName + "] " + toolResult);
          DataAccessFacade::getInstance().saveToolCall(
              "tc_" + taskId + "_" +
                  std::to_string(std::chrono::steady_clock::now()
                                     .time_since_epoch()
                                     .count()),
              taskId, actualToolName, toolArgsParsed.dump(), toolSuccess,
              toolSuccess ? toolResult : toolResult, toolExitCode);
        }

        if (SSEGateway::getInstance().isInitialized()) {
          json toolDbgMeta;
          toolDbgMeta["source"] = "tool";
          toolDbgMeta["tool_name"] = actualToolName;
          SSEGateway::getInstance().push(
              taskId, EventType::AgentMessage,
              "工具 [" + actualToolName + "]: " + toolResult, toolDbgMeta,
              SSEGateway::Channel::Debug, SSEGateway::Persist::Always);
        }
      }

      // ── <plan> ──
      auto planTags = tags.get("plan");
      if (!planTags.empty() && currentExpert->canModifyPlan) {
        planMgr.applyPlanTags(planTags, currentExpert->name);
        sessionHistory += "\n[system] 计划已更新\n";

        if (currentExpert->visibleTools.empty()) {
          sessionHistory += "\n[system] 计划已提交，当前阶段完成\n";
          break;
        }
      }

      // ── <write to="summary"> ──
      auto writeTags = tags.get("write");
      if (!writeTags.empty() && currentExpert->canWriteSummary) {
        for (const auto &wt : writeTags) {
          if (wt.attributes.value("to", "") == "summary") {
            ctx.summary = wt.content;
            sessionHistory += "\n[system] 摘要已更新: " + wt.content + "\n";
          }
        }
      }

      // ── <read from="global"> ──
      auto readTags = tags.get("read");
      if (!readTags.empty() && currentExpert->readGlobalActively) {
        for (const auto &rt : readTags) {
          if (rt.attributes.value("from", "") == "global" &&
              globalReadsUsed < currentExpert->maxGlobalRounds) {
            ++globalReadsUsed;
            // ★ v2: 从 global_context 表检索知识
            std::string globalContextStr;
            if (DataAccessFacade::getInstance().isInitialized()) {
              auto contexts = DataAccessFacade::getInstance().getGlobalContext(
                  ctx.globalId);
              if (contexts.empty()) {
                globalContextStr = "（暂无历史归档数据）";
              } else {
                // 格式化最近的知识片段
                size_t showCount = std::min(contexts.size(), size_t(3));
                for (size_t i = 0; i < showCount; ++i) {
                  globalContextStr += "\n[" + contexts[i].type + " from " +
                                      contexts[i].source_task_id + "] " +
                                      contexts[i].content + "\n";
                }
                if (contexts.size() > showCount) {
                  globalContextStr +=
                      "\n（还有 " +
                      std::to_string(contexts.size() - showCount) +
                      " 条历史记录，可再次检索）\n";
                }
              }
            } else {
              globalContextStr = "（数据库未初始化，无法检索）";
            }
            sessionHistory += "\n[system] 全局上下文检索（" +
                              std::to_string(globalReadsUsed) + "/" +
                              std::to_string(currentExpert->maxGlobalRounds) +
                              "）:\n" + globalContextStr + "\n";
          }
        }
      }

      // ── <status/> ──
      if (tags.has("status")) {
        sessionHistory +=
            "\n[system] " + planMgr.snapshot().toPromptFragment() + "\n";
      }

      if (tags.has("done") || tags.has("fail")) {
        if (tags.has("done")) {
          std::string doneContent =
              MessageBus::extractTagContent(lastOutput, "done");
          if (!doneContent.empty() &&
              doneContent.find_first_not_of(" \t\n\r") != std::string::npos) {
            ctx.summary += "\n\n[阶段完成] " + doneContent;
          }
        }
        if (tags.has("fail")) {
          std::string failContent =
              MessageBus::extractTagContent(lastOutput, "fail");
          if (!failContent.empty()) {
            ctx.summary += "\n\n[阶段失败] " + failContent;
          }
        }
        break;
      }

      if (tags.has("ask")) {
        sessionHistory += "\n[user] 已确认\n";
      }

      // Rejected commands still consume a round. Otherwise a model that keeps
      // returning unavailable or malformed tool calls can loop forever.
      int totalCmdCount = static_cast<int>(tags.get("cmd").size());
      if (totalCmdCount > 0 && acceptedCmdCount == 0) {
        sessionHistory += "\n[system] 你的上一轮指令全部格式不规范。";
        sessionHistory += "请检查工具名称和 JSON 参数格式后重试。\n";
      }

      sessionHistory += "\n[assistant] " + lastOutput + "\n";
      roundsLeft--;
    }

    // ── 超轮次 ──
    if (roundsLeft <= 0 && !MessageBus::hasTag(lastOutput, "done") &&
        !MessageBus::hasTag(lastOutput, "fail")) {
      return finalizeWithCriticalSummary(
          taskId,
          "Expert [" + currentExpert->name + "] 轮次耗尽（" +
              std::to_string(currentExpert->maxInternalRounds) + "轮）",
          "failed", sessionHistory, planMgr, ctx);
    }

    // ★★★ double_check ★★★
    // Expert 输出 <done> 后，用独立审核提示词调一次 LLM。
    // 审核也输出 <done> 则正常路由；输出其它内容则回注并给一轮继续执行。
    TagCollection finalTags = MessageBus::parse(lastOutput);
    Plan currentPlan = planMgr.snapshot();
    if (currentExpert->doubleCheck && finalTags.has("done") &&
        !finalTags.has("fail") &&
        LlmClientFacade::getInstance().isAvailable()) {

      // ★ 简化：用独立审核提示词，上下文只包含 goal + plan + summary + 最终输出
      std::string checkPrompt;
      checkPrompt =
          "你是任务审核助手，请判断 exec 是否真正完成了用户任务。\n\n";
      checkPrompt += "用户目标：\n" + ctx.goal + "\n\n";
      checkPrompt += "计划状态：\n" + currentPlan.toPromptFragment() + "\n";
      checkPrompt += "上下文摘要：\n" + ctx.summary + "\n";
      checkPrompt += "executor 最终输出：\n" + lastOutput + "\n\n";
      checkPrompt +=
          "如果 exec 确实完成了任务，只输出 <done>审核通过</done>。\n";
      checkPrompt +=
          "如果未完成或输出内容在敷衍，输出 <fail>未通过原因</fail>。";

      if (SSEGateway::getInstance().isInitialized()) {
        json dcMeta;
        dcMeta["source"] = "double_check";
        dcMeta["expert"] = currentExpert->name;
        SSEGateway::getInstance().push(
            taskId, EventType::AgentMessage,
            "[" + currentExpert->name + "] ⚡ 双检审核中…", dcMeta,
            SSEGateway::Channel::Status, SSEGateway::Persist::Always);
      }

      std::string provider =
          currentExpert->llmProvider.empty() ? "" : currentExpert->llmProvider;
      std::string model =
          currentExpert->llmModel.empty() ? "" : currentExpert->llmModel;
      int timeout =
          currentExpert->llmTimeout > 0 ? currentExpert->llmTimeout : 60;

      LlmResponse checkResp = LlmClientFacade::getInstance().chat(
          checkPrompt, provider, model, timeout);

      if (checkResp.success && !checkResp.content.empty()) {
        TagCollection checkTags = MessageBus::parse(checkResp.content);

        if (checkTags.has("done")) {
          // ✅ 审核通过：用审核结果替换原输出，正常路由
          lastOutput = checkResp.content;
          finalTags = checkTags;

          if (SSEGateway::getInstance().isInitialized()) {
            json dcOkMeta;
            dcOkMeta["source"] = "double_check";
            dcOkMeta["expert"] = currentExpert->name;
            dcOkMeta["result"] = "passed";
            SSEGateway::getInstance().push(
                taskId, EventType::AgentMessage,
                "[" + currentExpert->name + "] 双检通过 ✓", dcOkMeta,
                SSEGateway::Channel::Status, SSEGateway::Persist::Always);
          }
        } else {
          // ❌ 审核不通过：回注到 sessionHistory，给一轮继续执行
          sessionHistory += "\n[assistant] " + lastOutput + "\n";
          sessionHistory += "\n[system] 自检意见: " + checkResp.content + "\n";
          lastOutput = checkResp.content;
          roundsLeft = 1; // 给 1 轮修正机会
          firstRoundInExpert = false;

          if (SSEGateway::getInstance().isInitialized()) {
            json dcRetryMeta;
            dcRetryMeta["source"] = "double_check";
            dcRetryMeta["expert"] = currentExpert->name;
            dcRetryMeta["result"] = "retry";
            SSEGateway::getInstance().push(
                taskId, EventType::AgentMessage,
                "[" + currentExpert->name + "] 双检不通过，回炉修正…",
                dcRetryMeta, SSEGateway::Channel::Status,
                SSEGateway::Persist::Always);
          }

          continue; // 回到外层 while，同一 Expert 继续
        }
      } else {
        // LLM 调用失败：保守处理，跳过双检，按原始输出路由
        if (SSEGateway::getInstance().isInitialized()) {
          json dcErrMeta;
          dcErrMeta["source"] = "double_check";
          dcErrMeta["expert"] = currentExpert->name;
          dcErrMeta["result"] = "skipped";
          SSEGateway::getInstance().push(taskId, EventType::AgentMessage,
                                         "[" + currentExpert->name +
                                             "] 双检 LLM 调用失败，跳过审核",
                                         dcErrMeta, SSEGateway::Channel::Status,
                                         SSEGateway::Persist::Always);
        }
      }
    }

    std::string next =
        RouteEngine::resolve(*currentExpert, finalTags, currentPlan);

    if (cancelFlag && cancelFlag->load()) {
      return cancelledResult();
    }

    if (SSEGateway::getInstance().isInitialized()) {
      json routeMeta;
      routeMeta["expert"] = currentExpert->name;
      routeMeta["stage"] = "expert_done";
      routeMeta["next"] = next;
      SSEGateway::getInstance().push(
          taskId, EventType::AgentMessage,
          "Expert [" + currentExpert->name + "] 下班 → " + next, routeMeta,
          SSEGateway::Channel::Status, SSEGateway::Persist::Always);
    }

    if (next == "_done") {
      result.status = "completed";
      result.finalOutput = MessageBus::extractTagContent(lastOutput, "done");
      if (result.finalOutput.empty() && finalTags.has("fail")) {
        result.status = "failed";
        result.finalOutput = MessageBus::extractTagContent(lastOutput, "fail");
      }
      result.finalPlan = planMgr.snapshot();
      result.summary = ctx.summary;

      return result;
    }

    if (next == "_user_interrupt") {
      result.status = "completed";
      result.finalOutput = "任务已暂停等待用户确认";
      result.finalPlan = planMgr.snapshot();
      result.summary = ctx.summary;
      return result;
    }

    if (next.empty()) {
      return finalizeWithCriticalSummary(
          taskId, "Expert [" + currentExpert->name + "] 无匹配路由规则",
          "failed", sessionHistory, planMgr, ctx);
    }

    currentExpert = cfg.getExpert(next);
    if (!currentExpert) {
      return finalizeWithCriticalSummary(
          taskId, "路由目标 Expert [" + next + "] 未在配置中找到",
          "config_error", sessionHistory, planMgr, ctx);
    }
  }

  const std::string budgetMessage =
      "Expert 阶段预算已耗尽: taskId=" + taskId +
      ", configured maxSteps=" + std::to_string(options.maxSteps) +
      ", executed step count=" + std::to_string(expertSwitches);
  LOG_WARN("{}", budgetMessage);
  if (DataAccessFacade::getInstance().isInitialized()) {
    DataAccessFacade::getInstance().appendLog(taskId, "agent_loop",
                                              budgetMessage);
  }
  return finalizeWithCriticalSummary(taskId, budgetMessage, "failed",
                                     sessionHistory, planMgr, ctx);
}

AgentLoopResult AgentLoop::finalizeWithCriticalSummary(
    const std::string &taskId, const std::string &reason,
    const std::string &status, const std::string &sessionHistory,
    PlanManager &planMgr, const TaskContext &ctx) const {
  AgentLoopResult result;
  result.status = status;
  result.finalPlan = planMgr.snapshot();
  result.summary = ctx.summary;

  const std::string planFragment = result.finalPlan.toPromptFragment();
  (void)sessionHistory;

  result.finalOutput = "任务异常终止: " + reason + "\n" + planFragment;

  if (SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["stage"] = "critical_exit";
    meta["reason"] = reason;
    meta["status"] = status;
    SSEGateway::getInstance().push(
        taskId, EventType::AgentMessage, "[系统] " + reason, meta,
        SSEGateway::Channel::Status, SSEGateway::Persist::Always);
  }

  return result;
}

} // namespace codepilot

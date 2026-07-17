#include "AgentOrchestrator.h"
#include "AgentConfiguration.h"
#include "application/ToolSystem.h"
#include "domain/security/PermissionManager.h"
#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"
#include "facade/SSEGateway.h"

#include "common/logging/Logger.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace codepilot {

AgentOrchestrator &AgentOrchestrator::getInstance() {
  static AgentOrchestrator instance;
  return instance;
}

void AgentOrchestrator::init(const std::string &expertConfigPath) {
  std::lock_guard<std::mutex> lock(mutex_);
  expertConfigPath_ = expertConfigPath;
  AgentConfiguration::getInstance().init(expertConfigPath);
  initialized_ = true;

  // 自动创建默认 Global
  if (DataAccessFacade::getInstance().isInitialized()) {
    defaultGlobalId_ = DataAccessFacade::getInstance().ensureDefaultGlobal();
  }
}

bool AgentOrchestrator::isReady() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_ && AgentConfiguration::getInstance().isReady();
}

bool AgentOrchestrator::isReady(ExecutionMode mode) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_ && (mode == ExecutionMode::DirectAnswer ||
                          AgentConfiguration::getInstance().isReady());
}

// ── 前端注册 ──

int AgentOrchestrator::registerFrontend(const std::string &type) {
  std::lock_guard<std::mutex> lock(mutex_);
  int id = nextFrontendId_++;
  frontends_.push_back({id, type});

  if (SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["event"] = "frontend_connected";
    meta["frontend_id"] = id;
    meta["frontend_type"] = type;
    meta["total_frontends"] = static_cast<int>(frontends_.size());
    SSEGateway::getInstance().push(
        /*taskId=*/"", EventType::AgentMessage,
        "前端已连接: " + type + " (id=" + std::to_string(id) + ")", meta,
        SSEGateway::Channel::Status, SSEGateway::Persist::Never);
  }

  return id;
}

void AgentOrchestrator::unregisterFrontend(int id) {
  std::string type;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &f : frontends_) {
      if (f.id == id) {
        type = f.type;
        break;
      }
    }
    frontends_.erase(std::remove_if(frontends_.begin(), frontends_.end(),
                                    [id](const FrontendRegistration &f) {
                                      return f.id == id;
                                    }),
                     frontends_.end());
  }

  if (!type.empty() && SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["event"] = "frontend_disconnected";
    meta["frontend_id"] = id;
    meta["frontend_type"] = type;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      meta["total_frontends"] = static_cast<int>(frontends_.size());
    }
    SSEGateway::getInstance().push(
        /*taskId=*/"", EventType::AgentMessage,
        "前端已断开: " + type + " (id=" + std::to_string(id) + ")", meta,
        SSEGateway::Channel::Status, SSEGateway::Persist::Never);
  }
}

int AgentOrchestrator::frontendCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(frontends_.size());
}

// ── Global ID 解析 ──

std::string AgentOrchestrator::resolveGlobalId(const std::string &globalId) {
  if (!globalId.empty()) {
    // 验证 Global 是否存在
    if (DataAccessFacade::getInstance().isInitialized()) {
      auto g = DataAccessFacade::getInstance().getGlobal(globalId);
      if (g) {
        return globalId;
      }
    }
  }
  // 降级到默认 Global
  return defaultGlobalId_;
}

// ── 任务生命周期 ──

namespace {

bool isTerminalStatus(const std::string &status) {
  return status == "completed" || status == "failed" ||
         status == "cancelled" || status == "interrupted";
}

} // namespace

void AgentOrchestrator::startTask(const std::string &taskId,
                                  const std::string &globalId,
                                  const std::string &workspaceId,
                                  const std::string &goal,
                                  const TaskRunOptions &options) {
  if (DataAccessFacade::getInstance().isInitialized()) {
    const auto persistedTask = DataAccessFacade::getInstance().getTask(taskId);
    if (persistedTask && persistedTask->status == "interrupted") {
      throw std::logic_error("interrupted task cannot be started again");
    }
  }

  std::string resolvedGlobalId = resolveGlobalId(globalId);

  auto cancelFlag = std::make_shared<std::atomic<bool>>(false);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    ActiveTaskState state;
    state.taskId = taskId;
    state.globalId = resolvedGlobalId;
    state.workspaceId = workspaceId;
    state.goal = goal;
    state.status = "running";
    activeTasks_[taskId] = state;
    cancelFlags_[taskId] = cancelFlag;

    taskThreads_[taskId] =
        std::thread(&AgentOrchestrator::runTaskThread, this, taskId,
                    resolvedGlobalId, workspaceId, goal, options, cancelFlag);
    taskThreads_[taskId].detach();
  }
}

bool AgentOrchestrator::cancelTask(const std::string &taskId) {
  bool cancelled = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto stateIt = activeTasks_.find(taskId);
    if (stateIt == activeTasks_.end()) {
      LOG_INFO("AgentOrchestrator::cancelTask: task={} is no longer active",
               taskId);
      return false;
    }

    if (isTerminalStatus(stateIt->second.status)) {
      LOG_INFO(
          "AgentOrchestrator::cancelTask: task={} already terminal, status={}",
          taskId, stateIt->second.status);
      return false;
    }

    auto flagIt = cancelFlags_.find(taskId);
    if (flagIt == cancelFlags_.end()) {
      LOG_WARN("AgentOrchestrator::cancelTask: task={} has no cancel flag",
               taskId);
      return false;
    }

    flagIt->second->store(true);
    LlmClientFacade::getInstance().cancelRequests(flagIt->second);
    stateIt->second.status = "cancelled";
    stateIt->second.terminalEventSent = false;

    // Claiming and persisting the terminal state are one critical section.
    // finalizeTask() cannot claim completed/failed until cancelled is durable.
    if (DataAccessFacade::getInstance().isInitialized()) {
      cancelled = DataAccessFacade::getInstance().updateTaskStatus(
          taskId, "cancelled", "", "");
    }
    if (!cancelled) {
      flagIt->second->store(false);
      stateIt->second.status = "running";
      stateIt->second.terminalEventSent = false;
      LOG_ERROR("AgentOrchestrator::cancelTask: failed to persist task={}",
                taskId);
      return false;
    }
  }

  LOG_INFO("AgentOrchestrator::cancelTask: task={} result=cancelled", taskId);
  return cancelled;
}

// ── 状态快照 ──

std::vector<ActiveTaskState> AgentOrchestrator::activeTasks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ActiveTaskState> tasks;
  for (const auto &[id, state] : activeTasks_) {
    tasks.push_back(state);
  }
  return tasks;
}

ActiveTaskState
AgentOrchestrator::getTaskState(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = activeTasks_.find(taskId);
  if (it != activeTasks_.end())
    return it->second;
  return {};
}

// ── 完成任务收尾 ──

namespace {

EventType statusToTerminalEvent(const std::string &status) {
  if (status == "completed")
    return EventType::TaskCompleted;
  if (status == "failed")
    return EventType::TaskFailed;
  if (status == "cancelled")
    return EventType::TaskCancelled;
  return EventType::TaskFailed;
}

std::string statusToDbString(const std::string &status) {
  if (status == "completed" || status == "failed" || status == "cancelled" ||
      status == "interrupted")
    return status;
  return "failed";
}

} // namespace

void AgentOrchestrator::finalizeTask(const std::string &taskId,
                                     const std::string &globalId,
                                     const AgentLoopResult &result) {
  const std::string dbStatus = statusToDbString(result.status);
  const EventType terminalEvent = statusToTerminalEvent(result.status);

  std::string planJson;
  if (!result.finalPlan.steps.empty()) {
    json planDoc;
    planDoc["version"] = result.finalPlan.version;
    json stepsArr = json::array();
    for (const auto &s : result.finalPlan.steps) {
      json step;
      step["index"] = s.index;
      step["description"] = s.description;
      std::string stateStr;
      switch (s.state) {
      case Plan::Step::Done:
        stateStr = "done";
        break;
      case Plan::Step::InProgress:
        stateStr = "in_progress";
        break;
      case Plan::Step::Failed:
        stateStr = "failed";
        break;
      default:
        stateStr = "pending";
        break;
      }
      step["status"] = stateStr;
      stepsArr.push_back(step);
    }
    planDoc["steps"] = stepsArr;
    planJson = planDoc.dump();
  }

  bool ownsTerminalState = false;
  bool shouldSendCancellation = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto stateIt = activeTasks_.find(taskId);
    if (stateIt != activeTasks_.end() &&
        !isTerminalStatus(stateIt->second.status)) {
      stateIt->second.status = dbStatus;
      stateIt->second.terminalEventSent = true;
      if (DataAccessFacade::getInstance().isInitialized()) {
        ownsTerminalState =
            DataAccessFacade::getInstance().updateTaskStatus(
                taskId, dbStatus, planJson, result.finalOutput);
      }
      if (!ownsTerminalState) {
        LOG_ERROR("finalizeTask: failed to persist task={}, status={}", taskId,
                  dbStatus);
      }
    } else if (stateIt != activeTasks_.end() &&
               stateIt->second.status == "cancelled" &&
               !stateIt->second.terminalEventSent) {
      // cancelTask() owns and persists cancelled. The worker emits the event
      // only after it has stopped, so stream_end cannot precede business data.
      stateIt->second.terminalEventSent = true;
      shouldSendCancellation = true;
      LOG_INFO("finalizeTask: task={} acknowledging cancelled terminal state",
               taskId);
    } else if (stateIt != activeTasks_.end()) {
      LOG_INFO("finalizeTask: task={} terminal state already owned by {}",
               taskId, stateIt->second.status);
    }
  }

  std::string assistantMessageId;
  if (ownsTerminalState && !result.finalOutput.empty() &&
      DataAccessFacade::getInstance().isInitialized()) {
    try {
      const auto message =
          DataAccessFacade::getInstance().createFinalAssistantMessage(
              taskId, dbStatus == "completed" ? "result" : "error",
              result.finalOutput);
      assistantMessageId = message.id;
    } catch (const std::exception &error) {
      LOG_ERROR("finalizeTask: failed to persist assistant message for task={}: {}",
                taskId, error.what());
    }
  }

  // Only the thread that claimed and persisted the terminal state emits it.
  if (ownsTerminalState && SSEGateway::getInstance().isInitialized()) {
    if (!result.finalOutputSent && !result.finalOutput.empty()) {
      SSEGateway::getInstance().pushDialog(taskId, result.finalOutput);
    }
    json meta;
    meta["status"] = dbStatus;
    meta["global_id"] = globalId;
    meta["final_output_size"] = static_cast<int>(result.finalOutput.size());
    meta["expert_chain"] = result.expertChain;
    meta["plan_version"] = result.finalPlan.version;
    if (!assistantMessageId.empty()) {
      meta["assistant_message_id"] = assistantMessageId;
    }
    if (!result.summary.empty()) {
      meta["summary"] = result.summary;
    }
    SSEGateway::getInstance().push(taskId, terminalEvent, result.finalOutput,
                                   meta, SSEGateway::Channel::Status,
                                   SSEGateway::Persist::Always);
  }

  if (shouldSendCancellation && SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["status"] = "cancelled";
    meta["reason"] = "用户请求取消";
    SSEGateway::getInstance().push(
        taskId, EventType::TaskCancelled, "任务已被用户取消", meta,
        SSEGateway::Channel::Status, SSEGateway::Persist::Always);
  }

  // Archive only the winning completion/failure result. A cancelled task must
  // not receive writes from a losing worker after cancellation.
  if (ownsTerminalState && DataAccessFacade::getInstance().isInitialized()) {
    // ★ v2: 任务完成时归档到 global_context
    auto &facade = DataAccessFacade::getInstance();
    if (!result.summary.empty()) {
      facade.saveGlobalContext(globalId, taskId, "summary", result.summary);
    }
    if (!planJson.empty()) {
      facade.saveGlobalContext(globalId, taskId, "plan", planJson);
    }
    if (!result.finalOutput.empty()) {
      facade.saveGlobalContext(globalId, taskId, "output", result.finalOutput);
    }
  }

  // 3. 清理内存
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelFlags_.erase(taskId);
    taskThreads_.erase(taskId);
    activeTasks_.erase(taskId);
  }
}

// ── Pause / Resume ──

bool AgentOrchestrator::pauseTask(const std::string &taskId,
                                  const std::string &reason,
                                  const std::string &permissionRequestId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = activeTasks_.find(taskId);
  if (it == activeTasks_.end())
    return false;

  it->second.status = "paused";
  it->second.pauseReason = reason;
  it->second.permissionRequestId = permissionRequestId;

  LOG_INFO("AgentOrchestrator::pauseTask: task={}, reason={}, permId={}",
           taskId, reason, permissionRequestId);
  return true;
}

bool AgentOrchestrator::resumeTask(const std::string &taskId,
                                   const std::string &userMessage) {
  TaskSnapshot snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = activeTasks_.find(taskId);
    if (it == activeTasks_.end())
      return false;
    if (it->second.status != "paused") {
      LOG_WARN("resumeTask: task {} status is {}, not paused", taskId,
               it->second.status);
      return false;
    }

    auto snapIt = pausedSnapshots_.find(taskId);
    if (snapIt == pausedSnapshots_.end()) {
      LOG_ERROR("resumeTask: no snapshot found for paused task {}", taskId);
      return false;
    }
    snapshot = snapIt->second;
    pausedSnapshots_.erase(snapIt);
    it->second.status = "running";
  }

  // 有用户消息 → 通过 ResumeUtil 黑盒处理
  if (!userMessage.empty()) {
    auto resumeResult = ResumeUtil::prepareResume(snapshot, userMessage);
    if (!resumeResult.errorMessage.empty()) {
      LOG_ERROR("resumeTask: ResumeUtil failed: {}", resumeResult.errorMessage);
      // 失败仍用原 snapshot 恢复
    } else {
      snapshot = resumeResult.snapshot;
    }
  }

  // 启动恢复线程
  auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelFlags_[taskId] = cancelFlag;
    taskThreads_[taskId] = std::thread(&AgentOrchestrator::runResumeThread,
                                       this, taskId, snapshot, cancelFlag);
    taskThreads_[taskId].detach();
  }

  LOG_INFO("resumeTask: task={}, userMessage_len={}", taskId,
           userMessage.size());
  return true;
}

TaskSnapshot
AgentOrchestrator::getTaskSnapshot(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = pausedSnapshots_.find(taskId);
  if (it != pausedSnapshots_.end())
    return it->second;
  return TaskSnapshot{};
}

bool AgentOrchestrator::isPaused(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = activeTasks_.find(taskId);
  return it != activeTasks_.end() && it->second.status == "paused";
}

// ── 权限批处理 ──

void AgentOrchestrator::processPermissionBatch(
    const std::string &taskId,
    const std::vector<PermissionDecision> &decisions) {
  auto &pm = ToolSystem::getInstance().permissionManager();
  for (const auto &d : decisions) {
    pm.resolvePermission(d.requestId, d.approved);
  }

  LOG_INFO("processPermissionBatch: task={}, count={}", taskId,
           decisions.size());

  // 所有权限已处理 → 尝试恢复任务
  checkAndResumePausedTasks();
}

void AgentOrchestrator::checkAndResumePausedTasks() {
  std::vector<std::string> pausedTasks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[id, state] : activeTasks_) {
      if (state.status == "paused") {
        pausedTasks.push_back(id);
      }
    }
  }

  auto &pm = ToolSystem::getInstance().permissionManager();
  for (const auto &taskId : pausedTasks) {
    auto pending = pm.getPendingRequests(taskId);
    if (pending.empty()) {
      LOG_INFO("checkAndResumePausedTasks: auto-resuming task={}", taskId);
      resumeTask(taskId, "");
    }
  }
}

// ── 恢复执行 ──

void AgentOrchestrator::runResumeThread(
    const std::string &taskId, const TaskSnapshot &snapshot,
    std::shared_ptr<std::atomic<bool>> cancelFlag) {
  LOG_INFO("[ORCH DEBUG] runResumeThread START: taskId={}, expert={}", taskId,
           snapshot.currentExpert);

  try {
    AgentLoop agentLoop(expertConfigPath_);
    if (!agentLoop.isReady()) {
      LOG_ERROR("[ORCH DEBUG] runResumeThread: AgentLoop NOT READY");
      AgentLoopResult errorResult;
      errorResult.status = "config_error";
      errorResult.finalOutput = "Expert 配置未加载";
      finalizeTask(taskId, snapshot.globalId, errorResult);
      return;
    }

    // ★ 设置 onPause_ 回调 — Resume 过程中也可能再次暂停
    agentLoop.setOnPause([this, taskId](const TaskSnapshot &snap) {
      std::lock_guard<std::mutex> lock(mutex_);
      pausedSnapshots_[taskId] = snap;
      auto it = activeTasks_.find(taskId);
      if (it != activeTasks_.end()) {
        it->second.status = "paused";
        it->second.pauseReason = snap.pauseReason;
        it->second.permissionRequestId = snap.permissionRequestId;
      }
    });

    AgentLoopResult result = agentLoop.runFromSnapshot(snapshot, cancelFlag);

    LOG_INFO("[ORCH DEBUG] runResumeThread returned: status={}, output_len={}",
             result.status, result.finalOutput.size());
    finalizeTask(taskId, snapshot.globalId, result);
  } catch (const std::exception &e) {
    LOG_ERROR("[ORCH DEBUG] runResumeThread EXCEPTION: {}", e.what());
    AgentLoopResult errorResult;
    errorResult.status = "failed";
    errorResult.finalOutput = std::string("恢复执行异常: ") + e.what();
    finalizeTask(taskId, snapshot.globalId, errorResult);
  } catch (...) {
    LOG_ERROR("[ORCH DEBUG] runResumeThread UNKNOWN EXCEPTION");
    AgentLoopResult errorResult;
    errorResult.status = "failed";
    errorResult.finalOutput = "恢复执行未知异常";
    finalizeTask(taskId, snapshot.globalId, errorResult);
  }
}

// ── 内部异步执行（新任务） ──

AgentLoopResult AgentOrchestrator::runDirectAnswer(
    const std::string &taskId, const std::string &globalId,
    const std::string &workspaceId, const std::string &goal,
    const std::shared_ptr<std::atomic<bool>> &cancelFlag) {
  (void)globalId;
  (void)workspaceId;

  AgentLoopResult result;
  const auto isCancelled = [&cancelFlag]() {
    return cancelFlag && cancelFlag->load();
  };
  const auto appendExecutionLog = [&taskId](const std::string &status,
                                             const std::string &detail) {
    if (!DataAccessFacade::getInstance().isInitialized())
      return;
    try {
      DataAccessFacade::getInstance().appendLog(
          taskId, "direct_answer",
          json{{"status", status}, {"detail", detail}}.dump());
    } catch (const std::exception &e) {
      LOG_ERROR("[DIRECT ANSWER] Failed to append execution log: {}",
                e.what());
    }
  };

  if (isCancelled()) {
    result.status = "cancelled";
    result.finalOutput = "任务已取消";
    return result;
  }

  const std::string prompt =
      "请直接回答下面的用户问题。不要调用任何工具，不要修改任何文件，不要"
      "输出工具调用协议，也不要声称已经执行命令或修改项目。\n\n用户问题：\n" +
      goal;

  try {
    if (!LlmClientFacade::getInstance().isAvailable()) {
      result.status = "failed";
      result.finalOutput = "直接回答失败：大模型服务不可用";
      appendExecutionLog("failed", result.finalOutput);
      return result;
    }

    std::string answer;
    std::size_t sequence = 0;
    const std::string messageId = "agent_message:" + taskId + ":direct_answer";
    LlmClientFacade::getInstance().chatStream(
        prompt, [&](const std::string &chunk) {
          if (chunk.empty() || isCancelled()) {
            return;
          }
          answer += chunk;
          if (SSEGateway::getInstance().isInitialized()) {
            SSEGateway::getInstance().pushStream(taskId, messageId, chunk,
                                                 sequence++);
          }
        }, "auto", "", 0, cancelFlag);
    if (isCancelled()) {
      result.status = "cancelled";
      result.finalOutput = "任务已取消";
      return result;
    }

    if (answer.empty()) {
      result.status = "failed";
      result.finalOutput = "直接回答失败：大模型调用失败或返回空内容";
      appendExecutionLog("failed", result.finalOutput);
      return result;
    }
    if (answer.find_first_not_of(" \t\r\n") == std::string::npos) {
      result.status = "failed";
      result.finalOutput = "直接回答失败：大模型返回空内容";
      appendExecutionLog("failed", result.finalOutput);
      return result;
    }

    result.status = "completed";
    result.finalOutput = answer;
    if (SSEGateway::getInstance().isInitialized()) {
      SSEGateway::getInstance().pushStream(taskId, messageId, "", sequence,
                                           true, result.finalOutput);
      result.finalOutputSent = true;
    }
    appendExecutionLog("completed", "直接回答已生成并推送");
  } catch (const std::exception &e) {
    result.status = "failed";
    result.finalOutput = "直接回答失败：大模型请求异常";
    appendExecutionLog("failed", result.finalOutput);
  } catch (...) {
    result.status = "failed";
    result.finalOutput = "直接回答失败：未知异常";
    appendExecutionLog("failed", result.finalOutput);
  }

  return result;
}

void AgentOrchestrator::runTaskThread(
    const std::string &taskId, const std::string &globalId,
    const std::string &workspaceId, const std::string &goal,
    TaskRunOptions options,
    std::shared_ptr<std::atomic<bool>> cancelFlag) {
  LOG_INFO("[ORCH DEBUG] runTaskThread START: taskId={}, goal={}", taskId,
           goal.substr(0, 80));

  AgentLoopResult result;
  try {
    switch (options.mode) {
    case ExecutionMode::DirectAnswer:
      result =
          runDirectAnswer(taskId, globalId, workspaceId, goal, cancelFlag);
      break;
    case ExecutionMode::WorkspaceAgent:
    case ExecutionMode::Auto: {
      LOG_INFO("[ORCH DEBUG] Creating AgentLoop with config={}",
               expertConfigPath_);
      AgentLoop agentLoop(expertConfigPath_);
      if (!agentLoop.isReady()) {
        LOG_ERROR("[ORCH DEBUG] AgentLoop NOT READY, aborting");
        result.status = "config_error";
        result.finalOutput = "Expert 配置未加载";
        break;
      }

      LOG_INFO("[ORCH DEBUG] AgentLoop ready, calling agentLoop.run()... "
               "llmAvailable={}",
               LlmClientFacade::getInstance().isAvailable());
      result = agentLoop.run(taskId, globalId, workspaceId, goal, options,
                             cancelFlag);
      LOG_INFO(
          "[ORCH DEBUG] agentLoop.run() returned: status={}, output_len={}",
          result.status, result.finalOutput.size());
      break;
    }
    }
  } catch (const std::exception &e) {
    LOG_ERROR("[ORCH DEBUG] runTaskThread EXCEPTION: {}", e.what());
    result.status = "failed";
    result.finalOutput = std::string("Agent 执行异常: ") + e.what();
  } catch (...) {
    LOG_ERROR("[ORCH DEBUG] runTaskThread UNKNOWN EXCEPTION");
    result.status = "failed";
    result.finalOutput = "Agent 执行未知异常";
  }

  finalizeTask(taskId, globalId, result);
  LOG_INFO("[ORCH DEBUG] runTaskThread END: status={}", result.status);
}

} // namespace codepilot

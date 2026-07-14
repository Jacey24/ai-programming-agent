#include "AgentOrchestrator.h"
#include "AgentConfiguration.h"
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

void AgentOrchestrator::startTask(const std::string &taskId,
                                  const std::string &globalId,
                                  const std::string &workspaceId,
                                  const std::string &goal,
                                  const TaskRunOptions &options) {
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
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cancelFlags_.find(taskId);
  if (it == cancelFlags_.end())
    return false;
  it->second->store(true);

  auto stateIt = activeTasks_.find(taskId);
  if (stateIt != activeTasks_.end()) {
    stateIt->second.status = "cancelled";
  }

  if (SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["status"] = "cancelled";
    meta["reason"] = "用户请求取消";
    SSEGateway::getInstance().push(
        taskId, EventType::TaskCancelled, "任务已被用户取消", meta,
        SSEGateway::Channel::Status, SSEGateway::Persist::Always);
  }
  return true;
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
      status == "config_error")
    return status;
  return "failed";
}

} // namespace

void AgentOrchestrator::finalizeTask(const std::string &taskId,
                                     const std::string &globalId,
                                     const AgentLoopResult &result) {
  const std::string dbStatus = statusToDbString(result.status);
  const EventType terminalEvent = statusToTerminalEvent(result.status);

  // 1. 推送 SSE 终端事件
  if (SSEGateway::getInstance().isInitialized()) {
    json meta;
    meta["status"] = result.status;
    meta["global_id"] = globalId;
    meta["final_output_size"] = static_cast<int>(result.finalOutput.size());
    meta["expert_chain"] = result.expertChain;
    meta["plan_version"] = result.finalPlan.version;
    if (!result.summary.empty()) {
      meta["summary"] = result.summary;
    }
    SSEGateway::getInstance().push(taskId, terminalEvent, result.finalOutput,
                                   meta, SSEGateway::Channel::Status,
                                   SSEGateway::Persist::Always);
  }

  // 2. 更新 DB 任务状态
  if (DataAccessFacade::getInstance().isInitialized()) {
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
    DataAccessFacade::getInstance().updateTaskStatus(taskId, dbStatus, planJson,
                                                     result.finalOutput);

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
    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
      it->second.status = result.status;
      it->second.expertChain = result.expertChain;
    }
  }
}

// ── 内部异步执行 ──

void AgentOrchestrator::runTaskThread(
    const std::string &taskId, const std::string &globalId,
    const std::string &workspaceId, const std::string &goal,
    TaskRunOptions options,
    std::shared_ptr<std::atomic<bool>> cancelFlag) {
  LOG_INFO("[ORCH DEBUG] runTaskThread START: taskId={}, goal={}", taskId,
           goal.substr(0, 80));

  try {
    LOG_INFO("[ORCH DEBUG] Creating AgentLoop with config={}",
             expertConfigPath_);
    AgentLoop agentLoop(expertConfigPath_);

    if (!agentLoop.isReady()) {
      LOG_ERROR("[ORCH DEBUG] AgentLoop NOT READY, aborting");
      AgentLoopResult errorResult;
      errorResult.status = "config_error";
      errorResult.finalOutput = "Expert 配置未加载";
      finalizeTask(taskId, globalId, errorResult);
      LOG_INFO("[ORCH DEBUG] runTaskThread END (config_error)");
      return;
    }

    LOG_INFO("[ORCH DEBUG] AgentLoop ready, calling agentLoop.run()... "
             "llmAvailable={}",
             LlmClientFacade::getInstance().isAvailable());
    AgentLoopResult result =
        agentLoop.run(taskId, globalId, workspaceId, goal, options, cancelFlag);

    LOG_INFO("[ORCH DEBUG] agentLoop.run() returned: status={}, output_len={}",
             result.status, result.finalOutput.size());
    finalizeTask(taskId, globalId, result);
    LOG_INFO("[ORCH DEBUG] runTaskThread END (normal)");
  } catch (const std::exception &e) {
    LOG_ERROR("[ORCH DEBUG] runTaskThread EXCEPTION: {}", e.what());
    AgentLoopResult errorResult;
    errorResult.status = "failed";
    errorResult.finalOutput = std::string("Agent 执行异常: ") + e.what();
    finalizeTask(taskId, globalId, errorResult);
  } catch (...) {
    LOG_ERROR("[ORCH DEBUG] runTaskThread UNKNOWN EXCEPTION");
    AgentLoopResult errorResult;
    errorResult.status = "failed";
    errorResult.finalOutput = "Agent 执行未知异常";
    finalizeTask(taskId, globalId, errorResult);
  }
}

} // namespace codepilot

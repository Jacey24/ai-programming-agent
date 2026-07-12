#pragma once

#include "ResponseParser.h"
#include "RoleRegistry.h"
#include "TaskQueue.h"
#include "TaskState.h"
#include "event/EventTypes.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace codepilot {

using json = nlohmann::json;

class Planner;
class LlmClient;
struct ToolResult;
struct EventData;

struct AgentConfig {
  int maxSteps = 6;
  int maxRoundsPerStep = 3;
  int toolTimeoutSeconds = 60;
  bool autoRunSafeCommands = true;
  bool requireFileWritePermission = true;
  int maxRetries = 1;
  bool enableDeadlockCheck = true;
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
  std::vector<std::string> logs;
};

class Agent {
public:
  Agent(RoleRegistry &registry, Planner &planner);

  void setToolsDescription(const std::string &desc) { toolsDesc_ = desc; }

  // [已弃用] 保留仅为外部编译兼容；Agent 内部已改用 LlmClientFacade
  [[deprecated(
      "Agent uses LlmClientFacade internally; retained for API compatibility")]]
  void setLlmClient(std::shared_ptr<LlmClient> client) {
    (void)client;
  }

  void setConfig(const AgentConfig &config) { config_ = config; }

  AgentResult executeTask(const std::string &taskId,
                          const std::string &sessionId,
                          const std::string &workspaceId,
                          const std::string &goal);

  AgentResult executeDirectAnswer(const std::string &taskId,
                                  const std::string &sessionId,
                                  const std::string &workspaceId,
                                  const std::string &goal);

  std::string buildExecutorPrompt(const PlanStep &step, const RoleConfig &role,
                                  const std::string &goal) const;

private:
  // 工具辅助
  std::string toolsToString(const std::vector<std::string> &tools);
  std::string planToJson(const std::vector<PlanStep> &steps);
  std::string escapeJson(const std::string &s);
  static std::string iso8601Now();

  // 分级 SSE 推送辅助（通过 SSEGateway 门面）
  void publishDebugLog(const std::string &taskId, const std::string &content,
                       const std::string &source,
                       const std::string &stage = "") const;

  // 持久化（通过 DataAccessFacade 门面）
  void persistTaskEvent(const EventData &event) const;
  void persistToolCall(const std::string &taskId, const std::string &toolName,
                       const json &arguments, const ToolResult &result) const;
  void persistAndPublishFileChange(const std::string &taskId,
                                   const std::string &toolName,
                                   const json &arguments) const;

  static std::string generateEventId();
  static std::string generateToolCallId();
  static std::string generateFileChangeId();
  static bool isFileMutatingTool(const std::string &toolName);
  static std::string extractChangedPath(const std::string &toolName,
                                        const json &arguments);
  static std::string inferChangeType(const std::string &toolName);

  // 单步执行（构建 prompt → LLM 调用 → 解析 → 工具调用/完成）
  ParsedResponse executeSingleStep(const PlanStep &step, const RoleConfig &role,
                                   const std::string &goal,
                                   const std::string &taskId,
                                   const std::string &sessionId,
                                   const std::string &workspaceId,
                                   std::string &rawLlmOutput);

  bool isDeadlock(const std::vector<ParsedCommand> &commands) const;

  // 发布事件（通过 SSEGateway → EventBus → DataAccessFacade 三合一）
  void publishTaskEvent(const std::string &taskId, EventType eventType,
                        const std::string &content,
                        const std::string &metadataJson = "{}") const;

  static std::string normalizeToolName(const std::string &name);
  static json buildToolArguments(const std::string &toolName,
                                 const std::string &rawArgs);
  static json parseKeyValueArgs(const std::string &rawArgs);
  static json coerceScalar(const std::string &value);

  RoleRegistry &registry_;
  Planner &planner_;
  TaskQueue queue_;
  AgentConfig config_;
  std::vector<std::string> context_;
  std::string toolsDesc_;

  // 死锁检测状态
  std::vector<ParsedCommand> prevCommands_;
  int deadlockCount_ = 0;
};

} // namespace codepilot
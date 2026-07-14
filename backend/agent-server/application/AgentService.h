#pragma once

#include "domain/agent/Agent.h"
#include "domain/agent/TaskRunOptions.h"

#include <memory>
#include <string>

namespace codepilot {

class AgentService {
public:
  AgentResult runTask(const std::string &taskId, const std::string &sessionId,
                      const std::string &workspaceId, const std::string &goal,
                      const TaskRunOptions &options = {});

  static ExecutionMode resolveExecutionMode(const std::string &goal,
                                            ExecutionMode requestedMode);

  // Sprint 2：获取 Agent 实例，供外部使用 buildExecutorPrompt 等
  Agent *getAgent() { return agent_.get(); }

  // Sprint 2：获取工具描述文本（由 ToolSystem 提供）
  static std::string getToolsDescription();

private:
  std::unique_ptr<Agent> agent_;
};

} // namespace codepilot

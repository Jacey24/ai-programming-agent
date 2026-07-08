#include "ToolSystem.h"

namespace codepilot {

ToolSystem &ToolSystem::getInstance() {
  static ToolSystem instance;
  return instance;
}

void ToolSystem::init(const std::string &workspacePath,
                      const std::string & /*configPath*/) {
  if (initialized_) {
    return;
  }

  workspace_ = std::make_shared<Workspace>(workspacePath);
  shell_ = std::make_shared<BuiltinShell>(workspace_);
  registry_ = std::make_unique<ToolRegistry>();
  eventBus_ = std::make_shared<EventBus>();

  registerFileTools(*registry_, shell_);

  initialized_ = true;
}

ToolRegistry &ToolSystem::registry() { return *registry_; }

EventBus &ToolSystem::eventBus() { return *eventBus_; }

Workspace &ToolSystem::workspace() { return *workspace_; }

ToolResult ToolSystem::callTool(const std::string &name,
                                const ToolContext &context,
                                const json &arguments) {
  eventBus_->publish(EventData::Create(
      context.taskId, EventType::ToolStarted, "Starting tool: " + name,
      {{"tool_name", name}, {"arguments", arguments}}));

  ToolResult result = registry_->call(name, context, arguments);

  json metadata;
  metadata["tool_name"] = name;
  metadata["success"] = result.success;
  metadata["exit_code"] = result.exitCode;

  eventBus_->publish(EventData::Create(
      context.taskId, EventType::ToolFinished,
      result.success ? "Tool finished" : "Tool failed: " + result.error,
      metadata));

  return result;
}

} // namespace codepilot

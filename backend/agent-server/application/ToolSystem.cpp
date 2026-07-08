#include "ToolSystem.h"

#include "domain/tools/GitTool.h"
#include "domain/tools/ShellTool.h"

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
  runner_ = std::make_shared<ProcessRunner>();
  detector_ = std::make_shared<RiskDetector>();
  registry_ = std::make_unique<ToolRegistry>();
  eventBus_ = std::make_shared<EventBus>();

  registerFileTools(*registry_, shell_);

  // 注册 Sprint 2 工具
  registerShellTools(*registry_, runner_, detector_);
  registerGitTools(*registry_, runner_);

  // 注册分组提示词（Agent 可将这些注入 System Prompt）
  registry_->registerGroup(
      ToolGroups::FILE,
      "文件操作工具组：用于读取、写入、编辑项目文件。"
      "file.list 列出目录结构，file.read 读取文件内容，"
      "file.write 覆盖写入文件，file.apply_patch 应用补丁，"
      "cd/pwd 用于导航工作目录。写入和补丁操作需要用户确认。");

  registry_->registerGroup(
      ToolGroups::GIT,
      "Git 操作工具组：用于查看仓库状态和文件变更。"
      "git.status 查看仓库状态，git.diff 查看未暂存的变更，"
      "git.commit 暂存所有变更并提交。commit 操作需要用户确认。");

  registry_->registerGroup(
      ToolGroups::SHELL, "Shell 执行工具组：用于在执行 Shell 命令。"
                         "shell.run 在工作区内执行命令并返回输出。"
                         "风险命令（如 git push、sudo）会被检测并触发权限确认，"
                         "高危命令（如 rm -rf /）被系统阻止。");

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
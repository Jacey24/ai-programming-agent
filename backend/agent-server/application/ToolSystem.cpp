#include "ToolSystem.h"

namespace codepilot {

// ============================================================
// 单例实现
// ============================================================
ToolSystem &ToolSystem::getInstance() {
  static ToolSystem instance;
  return instance;
}

// ============================================================
// init() — 一次性初始化
// ============================================================
void ToolSystem::init(const std::string &workspacePath,
                      const std::string & /*configPath*/) {
  if (initialized_) {
    return; // 幂等
  }

  // 1. 基础设施层
  workspace_ = std::make_shared<Workspace>(workspacePath);
  runner_ = std::make_shared<ProcessRunner>();
  eventBus_ = std::make_shared<EventBus>();

  // 2. 安全层
  detector_ = std::make_shared<RiskDetector>();
  permissionManager_ = std::make_unique<PermissionManager>(eventBus_);

  // 3. 内部 Shell 和 Registry
  shell_ = std::make_shared<BuiltinShell>(workspace_);
  registry_ = std::make_unique<ToolRegistry>();

  // 4. 注册 Sprint 1 工具（文件操作）
  registerFileTools(*registry_, shell_);

  // 5. 注册 Sprint 2 工具
  registerShellTools(*registry_, runner_, detector_);
  registerGitTools(*registry_, runner_);

  initialized_ = true;
}

// ============================================================
// 子系统访问
// ============================================================
ToolRegistry &ToolSystem::registry() { return *registry_; }
EventBus &ToolSystem::eventBus() { return *eventBus_; }
Workspace &ToolSystem::workspace() { return *workspace_; }
RiskDetector &ToolSystem::riskDetector() { return *detector_; }
PermissionManager &ToolSystem::permissionManager() {
  return *permissionManager_;
}
ProcessRunner &ToolSystem::processRunner() { return *runner_; }

// ============================================================
// callTool() — 便捷调用（自动发布事件）
// ============================================================
ToolResult ToolSystem::callTool(const std::string &name,
                                const ToolContext &context,
                                const json &arguments) {
  // 发布 ToolStarted 事件
  eventBus_->publish(EventData::Create(
      context.taskId, EventType::ToolStarted, "开始执行 " + name,
      {{"tool_name", name}, {"arguments", arguments.dump()}}));

  // 调用工具
  ToolResult result = registry_->call(name, context, arguments);

  // 发布 ToolFinished 事件
  json meta;
  meta["tool_name"] = name;
  meta["success"] = result.success;
  meta["exit_code"] = result.exitCode;

  eventBus_->publish(EventData::Create(
      context.taskId, EventType::ToolFinished,
      result.success ? "执行完成" : "执行失败: " + result.error, meta));

  return result;
}

// ============================================================
// callToolWithPermission() — 带权限检查的工具调用
// ============================================================
ToolResult ToolSystem::callToolWithPermission(const std::string &name,
                                              const ToolContext &context,
                                              const json &arguments) {
  // 1. 获取工具并检测风险等级
  Tool *tool = registry_->getTool(name);
  if (!tool) {
    return ToolResult::Err("Tool not found: " + name);
  }

  RiskLevel level = tool->riskLevel(arguments);

  // 2. 如果 blocked，直接拒绝
  if (level == RiskLevel::Blocked) {
    json meta;
    meta["tool_name"] = name;
    meta["risk_level"] = riskLevelToString(level);
    meta["blocked"] = true;
    ToolResult r =
        ToolResult::Err("该操作已被系统阻止: " + name + " (风险等级: blocked)");
    r.metadata = meta;
    return r;
  }

  // 3. 如果需要权限，创建请求
  if (permissionManager_->requiresPermission(name, level, arguments)) {
    permissionManager_->createRequest(context.taskId, name, level, arguments);
    // 注意：这里仅演示创建请求并继续执行
    // 实际完整流程需要 Agent 等待用户确认后继续
  }

  // 4. 执行工具
  return callTool(name, context, arguments);
}

} // namespace codepilot
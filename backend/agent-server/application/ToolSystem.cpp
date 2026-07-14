#include "ToolSystem.h"
#include <fstream>
#include <sstream>

#include "domain/tools/GitTool.h"
#include "domain/tools/ShellTool.h"

namespace codepilot {

ToolSystem &ToolSystem::getInstance() {
  static ToolSystem instance;
  return instance;
}

// ============================================================
// 参数哈希（用于重复检测）
// ============================================================
std::string ToolSystem::hashArguments(const json &args) const {
  if (args.is_null() || args.empty()) {
    return "";
  }
  return args.dump();
}

// ============================================================
// 从配置文件加载工具覆盖
// ============================================================
bool ToolSystem::loadConfigFromFile(const std::string &configPath) {
  std::ifstream file(configPath);
  if (!file.is_open()) {
    return false;
  }

  json config;
  try {
    file >> config;
  } catch (const std::exception &) {
    return false;
  }

  std::unordered_map<std::string, ToolConfigOverride> newOverrides;

  for (auto it = config.begin(); it != config.end(); ++it) {
    const std::string &toolName = it.key();
    const json &toolConfig = it.value();

    ToolConfigOverride override;
    override.name = toolName;

    // 读取 risk_level 覆盖
    if (toolConfig.contains("risk_level") &&
        toolConfig["risk_level"].is_string()) {
      override.riskLevelOverride = toolConfig["risk_level"].get<std::string>();
    }

    // 读取 enabled/disabled
    if (toolConfig.contains("enabled") && toolConfig["enabled"].is_boolean()) {
      override.enabled = toolConfig["enabled"].get<bool>();
    } else {
      override.enabled = true; // 默认启用
    }

    newOverrides[toolName] = override;
  }

  // 原子化替换（写锁在 reloadConfig 中已持有）
  configOverrides_ = std::move(newOverrides);
  return true;
}

// ============================================================
// init() — 一次性初始化
// ============================================================
void ToolSystem::init(const std::string &workspacePath,
                      const std::string &configPath) {
  // init 本身也加写锁，防止并发 init
  std::unique_lock lock(mutex_);

  if (initialized_) {
    return;
  }

  configPath_ = configPath.empty() ? "config/tools.json" : configPath;

  // 1. 基础设施层
  workspace_ = std::make_shared<Workspace>(workspacePath);
  shell_ = std::make_shared<BuiltinShell>(workspace_);
  runner_ = std::make_shared<ProcessRunner>();
  detector_ = std::make_shared<RiskDetector>();
  registry_ = std::make_unique<ToolRegistry>();
  eventBus_ = std::make_shared<EventBus>();
  permissionManager_ = std::make_shared<PermissionManager>(eventBus_);
  debugger_ = std::make_unique<Debugger>(eventBus_); // 懒初始化，默认关闭

  registerFileTools(*registry_, shell_);

  // 注册 Shell 和 Git 工具（跨平台：Windows 使用 _popen，POSIX 使用 fork/exec）
  registerShellTools(*registry_, runner_, detector_);
  registerGitTools(*registry_, runner_);

  // 6. 注册分组提示词
  registry_->registerGroup(
      ToolGroups::FILE,
      "文件操作工具组：用于管理项目文件。"
      "file.list/file.read 浏览文件，"
      "file.write/file.search 编辑和搜索文件内容，"
      "file.mkdir/file.rmdir/file.remove 创建/删除目录和文件，"
      "file.copy/file.move 复制和移动文件，"
      "file.apply_patch 应用代码补丁，cd/pwd 导航工作目录。"
      "写入、删除、复制、移动操作需要用户确认。");

  registry_->registerGroup(
      ToolGroups::GIT,
      "Git 操作工具组：用于查看仓库状态和文件变更。"
      "git.status 查看仓库状态，git.diff 查看未暂存的变更，"
      "git.commit 暂存所有变更并提交。commit 操作需要用户确认。"
      "git.log 查看提交历史，git.branch 查看/创建分支。");

  registry_->registerGroup(
      ToolGroups::SHELL,
      "Shell 执行工具组：用于执行 Shell 命令。"
      "shell.run 在工作区内执行命令并返回输出。"
      "风险命令会被检测并触发权限确认，高危命令被系统阻止。");

  // 7. 尝试加载配置覆盖（失败不影响启动）
  loadConfigFromFile(configPath_);

  initialized_ = true;
}

// ============================================================
// 子系统访问（读锁保护）
// ============================================================
ToolRegistry &ToolSystem::registry() {
  std::shared_lock lock(mutex_);
  return *registry_;
}

EventBus &ToolSystem::eventBus() {
  std::shared_lock lock(mutex_);
  return *eventBus_;
}

Workspace &ToolSystem::workspace() {
  std::shared_lock lock(mutex_);
  return *workspace_;
}

RiskDetector &ToolSystem::riskDetector() {
  std::shared_lock lock(mutex_);
  return *detector_;
}

PermissionManager &ToolSystem::permissionManager() {
  std::shared_lock lock(mutex_);
  return *permissionManager_;
}

ProcessRunner &ToolSystem::processRunner() {
  std::shared_lock lock(mutex_);
  return *runner_;
}

// ============================================================
// callTool() — 线程安全版本（读锁）
// ============================================================
ToolResult ToolSystem::callTool(const std::string &name,
                                const ToolContext &context,
                                const json &arguments) {
  // 检查工具是否被禁用
  {
    std::shared_lock lock(mutex_);
    auto configIt = configOverrides_.find(name);
    if (configIt != configOverrides_.end() && !configIt->second.enabled) {
      return ToolResult::Err("工具已被管理员禁用: " + name);
    }
  }

  // 发布 ToolStarted 事件（EventBus 内部有锁）
  json startedMeta{{"tool_name", name}, {"arguments", arguments}};
  const auto callId = context.options.find("tool_call_id");
  if (callId != context.options.end())
    startedMeta["tool_call_id"] = callId->second;
  eventBus_->publish(EventData::Create(context.taskId, EventType::ToolStarted,
                                       "Starting tool: " + name, startedMeta));

  // ---- 重复指令检测（写锁范围尽可能小） ----
  std::string key = name + ":" + hashArguments(arguments);
  bool isDuplicate = false;

  {
    std::unique_lock lock(mutex_);
    auto it = callHistory_.find(key);
    if (it != callHistory_.end()) {
      it->second.count++;
      if (it->second.count >= 3 && !it->second.lastSuccess) {
        isDuplicate = true;
      }
    } else {
      CallHistoryEntry entry;
      entry.toolName = name;
      entry.argsHash = hashArguments(arguments);
      entry.count = 1;
      entry.lastSuccess = true;
      callHistory_[key] = entry;
      callOrder_.push_back(key);
    }
  }

  // ★ 断点检查：工具执行前
  // 仅当调试器启用且有对应断点时暂停
  json effectiveArgs = arguments;
  if (debugger_ && debugger_->isEnabled() &&
      debugger_->hasBreakpoint(name, BreakpointType::BeforeToolCall)) {
    json debugResult =
        debugger_->pauseBeforeTool(context.taskId, name, arguments);
    if (debugResult.contains("__skip__") &&
        debugResult["__skip__"].get<bool>()) {
      // 用户选择"跳过"此工具
      ToolResult skipResult;
      skipResult.success = debugResult.value("__skip_success__", false);
      skipResult.output = debugResult.value("__skip_output__", "");

      // 仍发布事件
      json meta;
      meta["tool_name"] = name;
      meta["success"] = skipResult.success;
      meta["skipped_by_debugger"] = true;
      eventBus_->publish(
          EventData::Create(context.taskId, EventType::ToolFinished,
                            "Tool skipped by debugger: " + name, meta));
      return skipResult;
    }
    effectiveArgs = debugResult;
  }

  // 调用工具（Registry 调用本身不持有锁，避免死锁）
  ToolResult result = registry_->call(name, context, effectiveArgs);

  // ★ 断点检查：工具执行后
  if (debugger_ && debugger_->isEnabled() &&
      debugger_->hasBreakpoint(name, BreakpointType::AfterToolCall)) {
    result =
        debugger_->pauseAfterTool(context.taskId, name, effectiveArgs, result);
  }

  // ---- 更新调用历史 & 统计（写锁） ----
  {
    std::unique_lock lock(mutex_);
    auto histIt = callHistory_.find(key);
    if (histIt != callHistory_.end()) {
      histIt->second.lastSuccess = result.success;
      histIt->second.output = result.output.substr(0, 100);
    }

    if (isDuplicate) {
      result.metadata["duplicate_warning"] =
          "该指令已连续执行 " + std::to_string(callHistory_[key].count) +
          " 次，可能陷入循环。建议检查当前任务状态或尝试不同操作。";
      result.metadata["duplicate_count"] = callHistory_[key].count;
    }

    // 统计更新
    auto statsIt = stats_.find(name);
    if (statsIt == stats_.end()) {
      ToolStats s;
      s.callCount = 1;
      s.successCount = result.success ? 1 : 0;
      s.failCount = result.success ? 0 : 1;
      stats_[name] = s;
    } else {
      statsIt->second.callCount++;
      if (result.success) {
        statsIt->second.successCount++;
      } else {
        statsIt->second.failCount++;
      }
    }
  }

  // 发布 ToolFinished 事件
  json meta;
  meta["tool_name"] = name;
  meta["success"] = result.success;
  meta["exit_code"] = result.exitCode;
  if (callId != context.options.end())
    meta["tool_call_id"] = callId->second;
  if (result.metadata.contains("duplicate_warning")) {
    meta["duplicate_warning"] = result.metadata["duplicate_warning"];
  }

  const std::string output =
      (result.success ? result.output : result.error).substr(0, 4000);
  eventBus_->publish(
      EventData::Create(context.taskId, EventType::ToolOutput, output, meta));
  eventBus_->publish(EventData::Create(
      context.taskId, EventType::ToolFinished,
      result.success ? "Tool finished" : "Tool failed: " + result.error, meta));

  return result;
}

// ============================================================
// callToolWithPermission() — 带权限检查的工具调用
// ★ Sprint 2 增强：集成同步等待机制
//   当工具需要权限确认时，会阻塞等待用户通过 API 同意/拒绝
// ============================================================
ToolResult ToolSystem::callToolWithPermission(const std::string &name,
                                              const ToolContext &context,
                                              const json &arguments) {
  // 1. 获取工具并检测风险等级
  Tool *tool = registry_->getTool(name);
  if (!tool) {
    json meta;
    meta["tool_name"] = name;
    meta["failure_reason"] = "TOOL_NOT_FOUND";
    ToolResult r = ToolResult::Err(
        "[TOOL_NOT_FOUND] 工具 \"" + name +
        "\" 未在工具系统中注册，调用被拒绝。请检查工具名称是否正确，"
        "或使用可用工具列表中的工具。");
    r.metadata = meta;
    return r;
  }

  // 检查配置覆盖中的风险等级
  RiskLevel level = tool->riskLevel(arguments);
  const auto optionEnabled = [&context](const char *name, bool fallback) {
    const auto it = context.options.find(name);
    return it == context.options.end() ? fallback : it->second == "true";
  };
  const bool fileWrite = name == "file.write" || name == "file.apply_patch";
  if (fileWrite && !optionEnabled("require_file_write_permission", true)) {
    level = RiskLevel::Safe;
  }
  if (name == "shell.run" && level == RiskLevel::Safe &&
      !optionEnabled("auto_run_safe_commands", true)) {
    level = RiskLevel::Medium;
  }
  {
    std::shared_lock lock(mutex_);
    auto configIt = configOverrides_.find(name);
    if (configIt != configOverrides_.end() &&
        !configIt->second.riskLevelOverride.empty()) {
      // 用配置中的 risk_level 覆盖工具的默认风险等级
      level = riskLevelFromString(configIt->second.riskLevelOverride);
    }
  }

  // 2. 如果 blocked，直接拒绝
  if (level == RiskLevel::Blocked) {
    json meta;
    meta["tool_name"] = name;
    meta["risk_level"] = "blocked";
    meta["failure_reason"] = "BLOCKED";
    meta["blocked"] = true;
    ToolResult r = ToolResult::Err(
        "[BLOCKED] 工具 \"" + name +
        "\" 已被系统永久阻止（风险等级: blocked）。该操作不可执行。"
        "请调整方案，使用安全性更高的替代操作完成目标。");
    r.metadata = meta;
    return r;
  }

  // 3. 如果需要权限，创建请求并等待用户决策
  if (permissionManager_->requiresPermission(name, level, arguments)) {
    auto request = permissionManager_->createRequest(context.taskId, name,
                                                     level, arguments);

    // ★ Sprint 2: 阻塞等待用户通过 API 确认
    //   郑嘉娴的 POST /api/v1/permissions/{id}/approve 端点
    //   会调用 PermissionManager::resolvePermission() 来唤醒
    bool approved = permissionManager_->waitForResolution(request.id);

    if (!approved) {
      // 重新读取请求以获取 waitForResolution 后更新的状态
      auto finalReq = permissionManager_->getRequestCopy(request.id);
      bool isExpired =
          finalReq && finalReq->status == PermissionStatus::Expired;

      json meta;
      meta["tool_name"] = name;
      meta["risk_level"] = riskLevelToString(level);
      meta["request_id"] = request.id;
      meta["permission_denied"] = true;
      std::string reason = "用户拒绝了该操作。";
      std::string tag = "PERMISSION_DENIED";
      if (isExpired) {
        reason = "权限请求已超时（120秒内未获得用户响应）。";
        tag = "PERMISSION_EXPIRED";
      }
      meta["failure_reason"] = tag;
      ToolResult r =
          ToolResult::Err("[" + tag + "] 工具 \"" + name + "\" (风险等级: " +
                          riskLevelToString(level) + ") " + reason +
                          " 请尝试使用不需要该权限的替代方案，或"
                          "等待用户在权限面板中批准后重试。");
      r.metadata = meta;
      return r;
    }
  }

  // 4. 执行工具（带断点检查）
  return callTool(name, context, arguments);
}

// ============================================================
// ★ debugger() — 调试器访问（自动创建）
// ============================================================
Debugger &ToolSystem::debugger() {
  if (!debugger_) {
    std::unique_lock lock(mutex_);
    if (!debugger_) {
      debugger_ = std::make_unique<Debugger>(eventBus_);
    }
  }
  return *debugger_;
}

void ToolSystem::setDebuggerEnabled(bool enabled) {
  if (enabled) {
    // 启用时确保 Debugger 已初始化
    debugger().setEnabled(true);
  } else if (debugger_) {
    debugger_->setEnabled(false);
  }
}

bool ToolSystem::isDebuggerEnabled() const {
  return debugger_ && debugger_->isEnabled();
}

// ============================================================
// ★ 新增：getToolSchemas() — 获取所有工具 schemas
// ============================================================
json ToolSystem::getToolSchemas() const {
  std::shared_lock lock(mutex_);
  return registry_->listSchemas();
}

// ============================================================
// ★ 新增：getToolSummaries() — 获取工具轻量摘要
// ============================================================
json ToolSystem::getToolSummaries() const {
  std::shared_lock lock(mutex_);
  return registry_->listSummaries();
}

// ============================================================
// ★ 新增：listToolNames() — 获取所有工具名称
// ============================================================
std::vector<std::string> ToolSystem::listToolNames() const {
  std::shared_lock lock(mutex_);
  return registry_->listToolNames();
}

// ============================================================
// ★ 新增：listToolsByGroup() — 按分组列出工具（文本格式）
// ============================================================
std::string ToolSystem::listToolsByGroup() const {
  std::shared_lock lock(mutex_);
  return registry_->listAvailableToolsByGroup();
}

// ============================================================
// getToolStats() — 获取单工具统计（读锁）
// ============================================================
json ToolSystem::getToolStats(const std::string &name) const {
  std::shared_lock lock(mutex_);
  auto it = stats_.find(name);
  if (it == stats_.end()) {
    json result;
    result["name"] = name;
    result["call_count"] = 0;
    result["success_count"] = 0;
    result["fail_count"] = 0;
    return result;
  }
  json result;
  result["name"] = name;
  result["call_count"] = it->second.callCount;
  result["success_count"] = it->second.successCount;
  result["fail_count"] = it->second.failCount;
  return result;
}

// ============================================================
// getAllToolStats() — 获取全部统计（读锁）
// ============================================================
json ToolSystem::getAllToolStats() const {
  std::shared_lock lock(mutex_);
  json result;
  result["tools"] = json::array();
  for (const auto &[name, stat] : stats_) {
    json item;
    item["name"] = name;
    item["call_count"] = stat.callCount;
    item["success_count"] = stat.successCount;
    item["fail_count"] = stat.failCount;
    result["tools"].push_back(item);
  }
  return result;
}

// ============================================================
// reloadConfig() — 配置热加载（写锁）
// ============================================================
bool ToolSystem::reloadConfig(const std::string &configPath) {
  std::string path = configPath.empty() ? configPath_ : configPath;

  // 写锁：阻止所有正在进行的工具调用
  std::unique_lock lock(mutex_);

  if (!loadConfigFromFile(path)) {
    return false;
  }

  configPath_ = path;
  return true;
}

// ============================================================
// isToolEnabled() — 检查工具是否启用（读锁）
// ============================================================
bool ToolSystem::isToolEnabled(const std::string &name) const {
  std::shared_lock lock(mutex_);
  auto it = configOverrides_.find(name);
  if (it == configOverrides_.end()) {
    return true; // 未配置覆盖时默认启用
  }
  return it->second.enabled;
}

} // namespace codepilot

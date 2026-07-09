#pragma once

#include "domain/security/PermissionManager.h"
#include "domain/security/RiskDetector.h"
#include "domain/tools/BuiltinShell.h"
#include "domain/tools/FileTool.h"
#include "domain/tools/Tool.h"
#include "domain/tools/ToolRegistry.h"
#include "event/EventBus.h"
#include "infrastructure/filesystem/Workspace.h"
#include "infrastructure/process/ProcessRunner.h"

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// 调用历史记录条目（用于重复指令检测）
// ============================================================
struct CallHistoryEntry {
  std::string toolName;
  std::string argsHash;
  std::string output;
  bool lastSuccess{false};
  int count{0};
};

// ============================================================
// 工具调用统计（运行时内存缓存）
// ============================================================
struct ToolStats {
  int callCount{0};
  int successCount{0};
  int failCount{0};
};

// ============================================================
// 工具配置覆盖（从 tools.json 加载）
// ============================================================
struct ToolConfigOverride {
  std::string name;
  std::string riskLevelOverride; // 空字符串表示不覆盖
  bool enabled{true};
};

// ============================================================
// ToolSystem - 工具系统统一入口（单例门面 Facade）
// 线程安全（读多写少使用 shared_mutex）
// 支持配置热加载（tools.json）
//
// Sprint 2 能力扩展（仅为其他模块暴露接口，不侵入外部代码）：
//   1. callToolWithPermission — 集成同步等待机制
//      → 调用 waitForResolution 阻塞等待用户确认
//   2. 元数据查询接口供 Agent 使用
//      → getToolSchemas / getToolSummaries / listToolNames / listToolsByGroup
//   3. getEventBus / getPermissionManager 供外部注册回调
// ============================================================
class ToolSystem {
public:
  static ToolSystem &getInstance();

  void init(const std::string &workspacePath,
            const std::string &configPath = "");

  bool isInitialized() const { return initialized_; }

  // --- 子系统访问（其他模块可通过门面获取） ---
  ToolRegistry &registry();
  EventBus &eventBus();
  Workspace &workspace();
  RiskDetector &riskDetector();
  PermissionManager &permissionManager();
  ProcessRunner &processRunner();

  // ============================================================
  // 工具调用接口
  // ============================================================

  // --- 直接调用工具（不检查权限） ---
  ToolResult callTool(const std::string &name, const ToolContext &context,
                      const json &arguments);

  // --- 带权限检查的工具调用（Sprint 2 Agent 主入口） ---
  // 如果是 medium/dangerous 操作，会自动创建权限请求
  // 并阻塞等待用户通过 API 确认后才能继续
  // 这依赖于 PermissionManager::waitForResolution 机制
  ToolResult callToolWithPermission(const std::string &name,
                                    const ToolContext &context,
                                    const json &arguments);

  // ============================================================
  // ★ 元数据查询接口（供 Agent 的 PromptBuilder 使用）
  // ============================================================

  // --- 获取所有工具 schemas（用于 LLM function calling） ---
  json getToolSchemas() const;

  // --- 获取工具轻量摘要（用于渐进式提示） ---
  json getToolSummaries() const;

  // --- 获取所有工具名称列表 ---
  std::vector<std::string> listToolNames() const;

  // --- 按分组列出工具（文本格式，用于 prompt） ---
  std::string listToolsByGroup() const;

  // ============================================================
  // 工具调用统计
  // ============================================================
  json getToolStats(const std::string &name) const;
  json getAllToolStats() const;

  // --- 配置热加载 ---
  bool reloadConfig(const std::string &configPath = "");

  // --- 查询工具配置覆盖 ---
  bool isToolEnabled(const std::string &name) const;

private:
  ToolSystem() = default;
  ~ToolSystem() = default;
  ToolSystem(const ToolSystem &) = delete;
  ToolSystem &operator=(const ToolSystem &) = delete;

  std::string hashArguments(const json &args) const;
  bool loadConfigFromFile(const std::string &configPath);

  mutable std::shared_mutex mutex_;

  std::shared_ptr<Workspace> workspace_;
  std::shared_ptr<BuiltinShell> shell_;
  std::unique_ptr<ToolRegistry> registry_;
  std::shared_ptr<EventBus> eventBus_;
  std::shared_ptr<ProcessRunner> runner_;
  std::shared_ptr<RiskDetector> detector_;
  std::shared_ptr<PermissionManager> permissionManager_;
  bool initialized_{false};
  std::string configPath_;

  // 重复指令检测 + 统计
  std::unordered_map<std::string, CallHistoryEntry> callHistory_;
  std::vector<std::string> callOrder_;
  mutable std::unordered_map<std::string, ToolStats> stats_;

  // 配置热加载
  std::unordered_map<std::string, ToolConfigOverride> configOverrides_;
};

} // namespace codepilot
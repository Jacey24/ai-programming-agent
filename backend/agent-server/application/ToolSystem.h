#pragma once

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
// ToolSystem - 工具系统统一入口（单例门面）
// 线程安全（读多写少使用 shared_mutex）
// 支持配置热加载（tools.json）
// ============================================================
class ToolSystem {
public:
  static ToolSystem &getInstance();

  void init(const std::string &workspacePath,
            const std::string &configPath = "");

  bool isInitialized() const { return initialized_; }

  ToolRegistry &registry();
  EventBus &eventBus();
  Workspace &workspace();

  ToolResult callTool(const std::string &name, const ToolContext &context,
                      const json &arguments);

  bool isInitialized() const { return initialized_; }
  ToolResult callToolWithPermission(const std::string &name,
                                    const ToolContext &context,
                                    const json &arguments);

  // --- 工具调用统计 ---
  json getToolStats(const std::string &name) const;
  json getAllToolStats() const;

  // --- 配置热加载 ---
  // 重新读取 tools.json，更新风险等级覆盖、启用/禁用状态
  // 此操作是线程安全的，不会影响正在执行的工具调用
  bool reloadConfig(const std::string &configPath = "");

  // --- 查询工具配置覆盖 ---
  // 检查某个工具是否被禁用
  bool isToolEnabled(const std::string &name) const;

private:
  ToolSystem() = default;
  ~ToolSystem() = default;
  ToolSystem(const ToolSystem &) = delete;
  ToolSystem &operator=(const ToolSystem &) = delete;

  std::string hashArguments(const json &args) const;

  // 从配置文件加载工具覆盖（内部实现）
  bool loadConfigFromFile(const std::string &configPath);

  // 读写锁（读多写少场景）
  mutable std::shared_mutex mutex_;

  std::shared_ptr<Workspace> workspace_;
  std::shared_ptr<BuiltinShell> shell_;
  std::unique_ptr<ToolRegistry> registry_;
  std::shared_ptr<EventBus> eventBus_;
  std::shared_ptr<ProcessRunner> runner_;
  std::shared_ptr<RiskDetector> detector_;
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
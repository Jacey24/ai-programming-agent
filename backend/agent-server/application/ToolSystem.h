#pragma once

#include "domain/security/PermissionManager.h"
#include "domain/security/RiskDetector.h"
#include "domain/tools/BuiltinShell.h"
#include "domain/tools/FileTool.h"
#include "domain/tools/GitTool.h"
#include "domain/tools/ShellTool.h"
#include "domain/tools/Tool.h"
#include "domain/tools/ToolRegistry.h"
#include "event/EventBus.h"
#include "infrastructure/filesystem/Workspace.h"
#include "infrastructure/process/ProcessRunner.h"
#include <memory>
#include <string>

namespace codepilot {

// ============================================================
// ToolSystem - 工具系统统一入口（单例门面）
//
// Sprint 2 挂载模块：
//   - ProcessRunner (infrastructure)
//   - RiskDetector (security)
//   - PermissionManager (security)
//   - ShellTool (shell.run)
//   - GitTool (git.status / git.diff)
//
// 使用方式：
//   1. 启动时 init() 一次
//   2. 其他模块通过 getInstance() 获取子系统引用
// ============================================================
class ToolSystem {
public:
  // --- 单例访问 ---
  static ToolSystem &getInstance();

  // --- 一次性初始化 ---
  void init(const std::string &workspacePath,
            const std::string &configPath = "");

  // --- 子系统访问 ---
  ToolRegistry &registry();
  EventBus &eventBus();
  Workspace &workspace();
  RiskDetector &riskDetector();
  PermissionManager &permissionManager();
  ProcessRunner &processRunner();

  // --- 便捷方法：调用工具 + 自动发布事件 ---
  ToolResult callTool(const std::string &name, const ToolContext &context,
                      const json &arguments);

  // --- 结合权限的工具调用 ---
  // 自动检查 RiskDetector → 需要权限时创建 PermissionRequest
  // 返回结果中 metadata 包含 permission_required 字段
  ToolResult callToolWithPermission(const std::string &name,
                                    const ToolContext &context,
                                    const json &arguments);

  // --- 状态查询 ---
  bool isInitialized() const { return initialized_; }

private:
  ToolSystem() = default;
  ~ToolSystem() = default;
  ToolSystem(const ToolSystem &) = delete;
  ToolSystem &operator=(const ToolSystem &) = delete;

  std::shared_ptr<Workspace> workspace_;
  std::shared_ptr<BuiltinShell> shell_;
  std::unique_ptr<ToolRegistry> registry_;
  std::shared_ptr<EventBus> eventBus_;
  std::shared_ptr<ProcessRunner> runner_;
  std::shared_ptr<RiskDetector> detector_;
  std::unique_ptr<PermissionManager> permissionManager_;
  bool initialized_{false};
};

} // namespace codepilot
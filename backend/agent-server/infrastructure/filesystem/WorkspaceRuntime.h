#pragma once

#include "Workspace.h"
#include "domain/tools/ToolRegistry.h"
#include "infrastructure/process/ProcessRunner.h"

#include <memory>
#include <mutex>
#include <string>

namespace codepilot {

// ============================================================
// WorkspaceRuntime — 单个工作区的完整运行环境
//
// 封装了一个 Workspace 需要的全部运行时资源：
//   - Workspace（文件系统）
//   - ProcessRunner（进程执行，默认 CWD 指向 workspace 根）
//   - ToolRegistry（该 workspace 的工具注册表）
//
// 设计原则：
//   - 每个 Workspace 拥有独立的执行环境
//   - 通过 executionMutex 保证对该 workspace 的串行工具调用
//   - 不持有全局单例，完全由 WorkspaceManager 管理生命周期
// ============================================================
struct WorkspaceRuntime {
  std::string workspaceId;
  std::string workspacePath;

  std::shared_ptr<Workspace> workspace;
  std::shared_ptr<ProcessRunner> processRunner;
  std::shared_ptr<ToolRegistry> toolRegistry;

  // 保证对该 workspace 的串行执行
  // 避免两个任务同时操作同一个 workspace 导致竞争
  std::mutex executionMutex;

  WorkspaceRuntime() = default;

  // 工厂方法：从路径创建完整的运行时
  static std::shared_ptr<WorkspaceRuntime> create(const std::string &id,
                                                  const std::string &path);

  // 重新设置 workspace 路径（用于热迁移）
  bool relocate(const std::string &newPath);
};

} // namespace codepilot
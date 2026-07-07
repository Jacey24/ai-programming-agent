#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "infrastructure/process/ProcessRunner.h"
#include <memory>

namespace codepilot {

// ============================================================
// GitTool — Git 状态和差异查询
//
// 工具名: git.status / git.diff
// 功能:   查看 Git 仓库状态和文件变更
//
// 对齐整体架构说明.md 第9.5节
// ============================================================
class GitStatusTool : public Tool {
public:
  explicit GitStatusTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
};

class GitDiffTool : public Tool {
public:
  explicit GitDiffTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
};

// --- 注册函数 ---
void registerGitTools(ToolRegistry &registry,
                      std::shared_ptr<ProcessRunner> runner);

} // namespace codepilot
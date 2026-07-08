#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "infrastructure/process/ProcessRunner.h"
#include <memory>

namespace codepilot {

// ============================================================
// GitTool — Git 操作工具集
//
// 已有工具: git.status / git.diff
// 新增工具: git.commit
//
// 对齐整体架构说明.md 第9.5节
// ============================================================
class GitStatusTool : public Tool {
public:
  explicit GitStatusTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  std::string group() const override { return ToolGroups::GIT; }
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
  std::string group() const override { return ToolGroups::GIT; }
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
};

// ============================================================
// GitCommitTool — 创建 git commit
//
// 功能: git add . + git commit -m "message"
// 风险等级: Dangerous（需要用户确认）
// ============================================================
class GitCommitTool : public Tool {
public:
  explicit GitCommitTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  std::string group() const override { return ToolGroups::GIT; }
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
};

// ============================================================
// GitLogTool — 查看提交历史
//
// 功能: git log --oneline -n <count>
// 风险等级: Safe
// ============================================================
class GitLogTool : public Tool {
public:
  explicit GitLogTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  std::string group() const override { return ToolGroups::GIT; }
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
};

// ============================================================
// GitBranchTool — 查看/创建分支
//
// 功能: git branch (list) / git branch <name> (create)
// 风险等级: Safe (list) / Medium (create)
// ============================================================
class GitBranchTool : public Tool {
public:
  explicit GitBranchTool(std::shared_ptr<ProcessRunner> runner);

  std::string name() const override;
  std::string description() const override;
  std::string group() const override { return ToolGroups::GIT; }
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

#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "domain/security/RiskDetector.h"
#include "infrastructure/process/ProcessRunner.h"
#include <memory>

namespace codepilot {

// ============================================================
// ShellTool — 执行 Shell 命令
//
// 工具名: shell.run
// 功能:   在工作区内执行 Shell 命令，返回 stdout/stderr
//
// 对齐整体架构说明.md 第9.5节
// ============================================================
class ShellRunTool : public Tool {
public:
  ShellRunTool(std::shared_ptr<ProcessRunner> runner,
               std::shared_ptr<RiskDetector> detector);

  std::string name() const override;
  std::string description() const override;
  std::string group() const override { return ToolGroups::SHELL; }
  ToolSchema schema() const override;
  RiskLevel riskLevel(const json &arguments) const override;
  ToolResult execute(const ToolContext &context,
                     const json &arguments) override;

private:
  std::shared_ptr<ProcessRunner> runner_;
  std::shared_ptr<RiskDetector> detector_;
};

// --- 注册函数 ---
void registerShellTools(ToolRegistry &registry,
                        std::shared_ptr<ProcessRunner> runner,
                        std::shared_ptr<RiskDetector> detector);

} // namespace codepilot
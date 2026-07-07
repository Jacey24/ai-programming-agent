#include "GitTool.h"
#include <sstream>

namespace codepilot {

// ============================================================
// GitStatusTool 实现
// ============================================================
GitStatusTool::GitStatusTool(std::shared_ptr<ProcessRunner> runner)
    : runner_(std::move(runner)) {}

std::string GitStatusTool::name() const { return "git.status"; }

std::string GitStatusTool::description() const {
  return "查看 Git 仓库状态，包括已修改、已暂存、未跟踪的文件";
}

ToolSchema GitStatusTool::schema() const {
  return {
      "git.status", "查看 Git 仓库状态，包括已修改、已暂存、未跟踪的文件", {}};
}

RiskLevel GitStatusTool::riskLevel(const json & /*arguments*/) const {
  return RiskLevel::Safe;
}

ToolResult GitStatusTool::execute(const ToolContext &context,
                                  const json & /*arguments*/) {
  // 设置工作目录
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  }

  ProcessResult pr = runner_->execute("git status", 30);

  ToolResult result;
  result.success = pr.success;
  result.exitCode = pr.exitCode;

  if (pr.success) {
    result.output = pr.output;
  } else {
    std::ostringstream oss;
    oss << "Git status failed (exit code " << pr.exitCode << ")\n";
    if (!pr.output.empty()) {
      oss << pr.output << "\n";
    }
    if (!pr.errorOutput.empty()) {
      oss << pr.errorOutput;
    }
    result.error = oss.str();
  }

  return result;
}

// ============================================================
// GitDiffTool 实现
// ============================================================
GitDiffTool::GitDiffTool(std::shared_ptr<ProcessRunner> runner)
    : runner_(std::move(runner)) {}

std::string GitDiffTool::name() const { return "git.diff"; }

std::string GitDiffTool::description() const {
  return "查看 Git 文件变更详情（未暂存的 diff）";
}

ToolSchema GitDiffTool::schema() const {
  return {"git.diff",
          "查看 Git 文件变更详情（未暂存的 diff）",
          {
              {"path", "string",
               "指定文件路径（可选，为空则显示所有文件 diff）", false, ""},
          }};
}

RiskLevel GitDiffTool::riskLevel(const json & /*arguments*/) const {
  return RiskLevel::Safe;
}

ToolResult GitDiffTool::execute(const ToolContext &context,
                                const json &arguments) {
  // 设置工作目录
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  }

  std::string cmd = "git diff";
  if (arguments.contains("path")) {
    cmd += " -- \"" + arguments["path"].get<std::string>() + "\"";
  }

  ProcessResult pr = runner_->execute(cmd, 30);

  ToolResult result;
  result.success = pr.success;
  result.exitCode = pr.exitCode;

  if (pr.success) {
    result.output = pr.output;
  } else {
    std::ostringstream oss;
    oss << "Git diff failed (exit code " << pr.exitCode << ")\n";
    if (!pr.output.empty()) {
      oss << pr.output << "\n";
    }
    if (!pr.errorOutput.empty()) {
      oss << pr.errorOutput;
    }
    result.error = oss.str();
  }

  return result;
}

// ============================================================
// 注册函数
// ============================================================
void registerGitTools(ToolRegistry &registry,
                      std::shared_ptr<ProcessRunner> runner) {
  registry.registerTool(std::make_unique<GitStatusTool>(runner));
  registry.registerTool(std::make_unique<GitDiffTool>(runner));
}

} // namespace codepilot
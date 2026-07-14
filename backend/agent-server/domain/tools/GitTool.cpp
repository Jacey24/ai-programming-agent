#include "GitTool.h"
#include "infrastructure/filesystem/WorkspaceManager.h"
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
// GitCommitTool 实现
//
// 执行流程: git add . → git commit -m "message"
// 风险等级: Dangerous（需要用户确认）
// ============================================================
GitCommitTool::GitCommitTool(std::shared_ptr<ProcessRunner> runner)
    : runner_(std::move(runner)) {}

std::string GitCommitTool::name() const { return "git.commit"; }

std::string GitCommitTool::description() const {
  return "创建 Git commit：暂存所有变更（git add .）并提交";
}

ToolSchema GitCommitTool::schema() const {
  return {"git.commit",
          "创建 Git commit：暂存所有变更（git add .）并提交",
          {
              {"message", "string", "commit 信息，描述本次变更内容", true, ""},
          }};
}

RiskLevel GitCommitTool::riskLevel(const json & /*arguments*/) const {
  // git commit 修改仓库历史，需要用户确认
  return RiskLevel::Dangerous;
}

ToolResult GitCommitTool::execute(const ToolContext &context,
                                  const json &arguments) {
  // 设置工作目录
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  }

  // 获取 commit message
  std::string message = "update";
  if (arguments.contains("message")) {
    message = arguments["message"].get<std::string>();
  }

  // Step 1: git add .
  ProcessResult addResult = runner_->execute("git add .", 30);
  if (!addResult.success) {
    std::ostringstream oss;
    oss << "git add failed (exit code " << addResult.exitCode << ")\n";
    if (!addResult.errorOutput.empty()) {
      oss << addResult.errorOutput;
    }
    return ToolResult::Err(oss.str(), addResult.exitCode);
  }

  // Step 2: git commit -m "message"
  std::string commitCmd = "git commit -m \"" + message + "\"";
  ProcessResult commitResult = runner_->execute(commitCmd, 30);

  ToolResult result;
  result.success = commitResult.success;
  result.exitCode = commitResult.exitCode;

  if (commitResult.success) {
    result.output = commitResult.output;
    result.metadata = {{"staged_files", addResult.output.empty()
                                            ? "all changes staged"
                                            : addResult.output}};
  } else {
    std::ostringstream oss;
    oss << "Git commit failed (exit code " << commitResult.exitCode << ")\n";
    if (!commitResult.output.empty()) {
      oss << commitResult.output << "\n";
    }
    if (!commitResult.errorOutput.empty()) {
      oss << commitResult.errorOutput;
    }
    result.error = oss.str();
  }

  return result;
}

// ============================================================
// GitLogTool 实现
// ============================================================
GitLogTool::GitLogTool(std::shared_ptr<ProcessRunner> runner)
    : runner_(std::move(runner)) {}

std::string GitLogTool::name() const { return "git.log"; }

std::string GitLogTool::description() const {
  return "查看 Git 提交历史（--oneline 格式）";
}

ToolSchema GitLogTool::schema() const {
  return {"git.log",
          "查看 Git 提交历史（--oneline 格式）",
          {
              {"count", "integer", "要显示的提交数量（可选，默认 20）", false,
               "20"},
          }};
}

RiskLevel GitLogTool::riskLevel(const json & /*arguments*/) const {
  return RiskLevel::Safe;
}

ToolResult GitLogTool::execute(const ToolContext &context,
                               const json &arguments) {
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  }

  int count = arguments.contains("count") ? arguments["count"].get<int>() : 20;
  if (count < 1)
    count = 1;
  if (count > 100)
    count = 100;

  std::string cmd = "git log --oneline -n " + std::to_string(count);
  ProcessResult pr = runner_->execute(cmd, 30);

  ToolResult result;
  result.success = pr.success;
  result.exitCode = pr.exitCode;

  if (pr.success) {
    result.output = pr.output;
  } else {
    std::ostringstream oss;
    oss << "Git log failed (exit code " << pr.exitCode << ")\n";
    if (!pr.output.empty())
      oss << pr.output << "\n";
    if (!pr.errorOutput.empty())
      oss << pr.errorOutput;
    result.error = oss.str();
  }

  return result;
}

// ============================================================
// GitBranchTool 实现
// ============================================================
GitBranchTool::GitBranchTool(std::shared_ptr<ProcessRunner> runner)
    : runner_(std::move(runner)) {}

std::string GitBranchTool::name() const { return "git.branch"; }

std::string GitBranchTool::description() const { return "查看或创建 Git 分支"; }

ToolSchema GitBranchTool::schema() const {
  return {
      "git.branch",
      "查看或创建 Git 分支",
      {
          {"action", "string",
           "操作类型: \"list\" 查看分支, \"create\" 创建新分支", false, "list"},
          {"name", "string", "新分支名称（action 为 create 时必填）", false,
           ""},
      }};
}

RiskLevel GitBranchTool::riskLevel(const json &arguments) const {
  std::string action = arguments.contains("action")
                           ? arguments["action"].get<std::string>()
                           : "list";
  if (action == "create") {
    return RiskLevel::Medium;
  }
  return RiskLevel::Safe;
}

ToolResult GitBranchTool::execute(const ToolContext &context,
                                  const json &arguments) {
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  }

  std::string action = arguments.contains("action")
                           ? arguments["action"].get<std::string>()
                           : "list";

  std::string cmd;
  if (action == "create") {
    if (!arguments.contains("name") ||
        arguments["name"].get<std::string>().empty()) {
      return ToolResult::Err(
          "Missing required parameter: name (for create action)");
    }
    std::string branchName = arguments["name"].get<std::string>();
    cmd = "git branch " + branchName;
  } else {
    cmd = "git branch";
  }

  ProcessResult pr = runner_->execute(cmd, 30);

  ToolResult result;
  result.success = pr.success;
  result.exitCode = pr.exitCode;

  if (pr.success) {
    result.output = pr.output;
  } else {
    std::ostringstream oss;
    oss << "Git branch failed (exit code " << pr.exitCode << ")\n";
    if (!pr.output.empty())
      oss << pr.output << "\n";
    if (!pr.errorOutput.empty())
      oss << pr.errorOutput;
    result.error = oss.str();
  }

  return result;
}

// ============================================================
// 注册函数（含 git.commit / git.log / git.branch）
// ============================================================
void registerGitTools(ToolRegistry &registry,
                      std::shared_ptr<ProcessRunner> runner) {
  registry.registerTool(std::make_unique<GitStatusTool>(runner));
  registry.registerTool(std::make_unique<GitDiffTool>(runner));
  registry.registerTool(std::make_unique<GitCommitTool>(runner));

  // Sprint 2.5+ 新工具
  registry.registerTool(std::make_unique<GitLogTool>(runner));
  registry.registerTool(std::make_unique<GitBranchTool>(runner));
}

} // namespace codepilot

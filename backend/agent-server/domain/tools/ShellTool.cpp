#include "ShellTool.h"
#include <sstream>

namespace codepilot {

ShellRunTool::ShellRunTool(std::shared_ptr<ProcessRunner> runner,
                           std::shared_ptr<RiskDetector> detector)
    : runner_(std::move(runner)), detector_(std::move(detector)) {}

std::string ShellRunTool::name() const { return "shell.run"; }

std::string ShellRunTool::description() const {
  return "在工作区内执行 Shell 命令，返回标准输出和错误输出";
}

ToolSchema ShellRunTool::schema() const {
  return {
      "shell.run",
      "在工作区内执行 Shell 命令，返回标准输出和错误输出",
      {
          {"command", "string", "要执行的 Shell 命令", true, ""},
          {"cwd", "string", "工作目录（可选，默认 workspace 根目录）", false,
           ""},
          {"timeout", "integer", "超时时间/秒（可选，默认 60）", false, "60"},
      }};
}

RiskLevel ShellRunTool::riskLevel(const json &arguments) const {
  if (detector_ && arguments.contains("command")) {
    return detector_->detectCommand(arguments["command"].get<std::string>());
  }
  return RiskLevel::Medium;
}

ToolResult ShellRunTool::execute(const ToolContext &context,
                                 const json &arguments) {
  // 参数校验
  if (!arguments.contains("command")) {
    return ToolResult::Err("Missing required parameter: command");
  }

  std::string command = arguments["command"].get<std::string>();
  int timeout = 60;
  if (arguments.contains("timeout")) {
    timeout = arguments["timeout"].get<int>();
  }

  // 设置工作目录
  if (!context.workspacePath.empty()) {
    runner_->setWorkingDirectory(context.workspacePath);
  } else if (arguments.contains("cwd")) {
    runner_->setWorkingDirectory(arguments["cwd"].get<std::string>());
  }

  // 执行命令
  ProcessResult pr = runner_->execute(command, timeout);

  // 构建结果
  ToolResult result;
  result.success = pr.success;
  result.exitCode = pr.exitCode;

  if (pr.success) {
    result.output = pr.output;
  } else {
    std::ostringstream oss;
    oss << "Command failed (exit code " << pr.exitCode << ")\n";
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
void registerShellTools(ToolRegistry &registry,
                        std::shared_ptr<ProcessRunner> runner,
                        std::shared_ptr<RiskDetector> detector) {
  registry.registerTool(std::make_unique<ShellRunTool>(runner, detector));
}

} // namespace codepilot
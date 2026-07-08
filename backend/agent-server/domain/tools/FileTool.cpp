#include "FileTool.h"

#include <memory>
#include <utility>

namespace codepilot {

// ============================================================
// FileListTool - implements file.list
// ============================================================
class FileListTool : public Tool {
public:
  explicit FileListTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.list"; }

  std::string description() const override {
    return "List files and directories in the workspace with recursion depth "
           "control";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.list",
            "List files and directories in the workspace",
            {{"path", "string",
              "relative path to list (optional, default root)", false, ""},
             {"depth", "integer", "recursion depth 1-5 (optional, default 2)",
              false, "2"}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->listFiles(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// FileReadTool - implements file.read
// ============================================================
class FileReadTool : public Tool {
public:
  explicit FileReadTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.read"; }

  std::string description() const override {
    return "Read contents of a file in the workspace, supports line range";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.read",
            "Read file contents with optional line range",
            {{"path", "string", "relative file path", true, ""},
             {"start_line", "integer", "start line (optional, default 1)",
              false, "1"},
             {"end_line", "integer", "end line (optional, default end of file)",
              false, "-1"}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->readFile(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// FileWriteTool - implements file.write
// ============================================================
class FileWriteTool : public Tool {
public:
  explicit FileWriteTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.write"; }

  std::string description() const override {
    return "Write content to a file (overwrite mode), requires user "
           "confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.write",
            "Write file content (overwrite mode, requires confirmation)",
            {{"path", "string", "relative file path", true, ""},
             {"content", "string", "file content to write", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->writeFile(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// FileApplyPatchTool - implements file.apply_patch
// ============================================================
class FileApplyPatchTool : public Tool {
public:
  explicit FileApplyPatchTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.apply_patch"; }

  std::string description() const override {
    return "Apply a code patch to a file, requires user confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.apply_patch",
            "Apply code patch to file (requires confirmation)",
            {{"file_path", "string", "relative file path to modify", true, ""},
             {"patch", "string", "patch content in diff format", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->applyPatch(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// ChangeDirTool - implements cd
// ============================================================
class ChangeDirTool : public Tool {
public:
  explicit ChangeDirTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "cd"; }
  std::string description() const override {
    return "Change current working directory";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"cd",
            "Change current working directory",
            {{"path", "string", "relative path to change to", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->changeDirectory(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// PwdTool - implements pwd
// ============================================================
class PwdTool : public Tool {
public:
  explicit PwdTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "pwd"; }
  std::string description() const override {
    return "Print current working directory path";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"pwd", "Print current working directory path", {}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->getWorkingDirectory(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// Factory: register all file/directory tools to ToolRegistry
// ============================================================
void registerFileTools(ToolRegistry &registry,
                       std::shared_ptr<BuiltinShell> shell) {
  registry.registerTool(std::make_unique<FileListTool>(shell));
  registry.registerTool(std::make_unique<FileReadTool>(shell));
  registry.registerTool(std::make_unique<FileWriteTool>(shell));
  registry.registerTool(std::make_unique<FileApplyPatchTool>(shell));
  registry.registerTool(std::make_unique<ChangeDirTool>(shell));
  registry.registerTool(std::make_unique<PwdTool>(shell));
}

} // namespace codepilot
#include "FileTool.h"

#include <memory>
#include <utility>

namespace codepilot {

class FileListTool final : public Tool {
public:
  explicit FileListTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.list"; }

  std::string description() const override {
    return "List files and directories inside the workspace.";
  }

  ToolSchema schema() const override {
    return {"file.list",
            description(),
            {{"path", "string", "Workspace-relative directory path.", false,
              ""},
             {"depth", "integer", "Recursive listing depth from 1 to 5.",
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

class FileReadTool final : public Tool {
public:
  explicit FileReadTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.read"; }

  std::string description() const override {
    return "Read a text file inside the workspace.";
  }

  ToolSchema schema() const override {
    return {"file.read",
            description(),
            {{"path", "string", "Workspace-relative file path.", true, ""},
             {"start_line", "integer", "First line to read, 1-based.", false,
              "1"},
             {"end_line", "integer",
              "Last line to read. Use -1 to read through the end.", false,
              "-1"}}};
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

class FileWriteTool final : public Tool {
public:
  explicit FileWriteTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.write"; }

  std::string description() const override {
    return "Write text content to a file inside the workspace.";
  }

  ToolSchema schema() const override {
    return {"file.write",
            description(),
            {{"path", "string", "Workspace-relative file path.", true, ""},
             {"content", "string", "Text content to write.", true, ""}}};
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

class FileApplyPatchTool final : public Tool {
public:
  explicit FileApplyPatchTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.apply_patch"; }

  std::string description() const override {
    return "Apply a patch to a file inside the workspace.";
  }

  ToolSchema schema() const override {
    return {"file.apply_patch",
            description(),
            {{"file_path", "string", "Workspace-relative file path.", true,
              ""},
             {"patch", "string", "Patch content.", true, ""}}};
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

class ChangeDirTool final : public Tool {
public:
  explicit ChangeDirTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "cd"; }

  std::string description() const override {
    return "Change the current workspace-relative directory.";
  }

  ToolSchema schema() const override {
    return {"cd",
            description(),
            {{"path", "string", "Workspace-relative directory path.", true,
              ""}}};
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

class PwdTool final : public Tool {
public:
  explicit PwdTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "pwd"; }

  std::string description() const override {
    return "Return the current workspace-relative directory.";
  }

  ToolSchema schema() const override { return {"pwd", description(), {}}; }

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

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
// 新: FileSearchTool - implements file.search
// 在工作区内递归搜索文件内容（类似 grep）
// ============================================================
class FileSearchTool : public Tool {
public:
  explicit FileSearchTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.search"; }
  std::string description() const override {
    return "Search file contents in the workspace using regex pattern (like "
           "grep)";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {
        "file.search",
        "Search file contents recursively using regex pattern",
        {{"pattern", "string", "regex pattern to search for", true, ""},
         {"root", "string",
          "relative root path to search in (optional, default workspace root)",
          false, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->searchFiles(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// 新: FileMkdirTool - implements file.mkdir
// ============================================================
class FileMkdirTool : public Tool {
public:
  explicit FileMkdirTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.mkdir"; }
  std::string description() const override {
    return "Create a directory (creates parent directories if needed)";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {
        "file.mkdir",
        "Create directory (creates parent directories if needed)",
        {{"path", "string", "relative path of directory to create", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Safe;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->makeDirectory(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// 新: FileRmdirTool - implements file.rmdir
// ============================================================
class FileRmdirTool : public Tool {
public:
  explicit FileRmdirTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.rmdir"; }
  std::string description() const override {
    return "Remove an empty directory, requires user confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {
        "file.rmdir",
        "Remove an empty directory",
        {{"path", "string", "relative path of directory to remove", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->removeDirectory(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// 新: FileRemoveTool - implements file.remove
// ============================================================
class FileRemoveTool : public Tool {
public:
  explicit FileRemoveTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.remove"; }
  std::string description() const override {
    return "Remove a file or directory recursively, requires user confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.remove",
            "Remove file or directory recursively (requires confirmation)",
            {{"path", "string", "relative path of file or directory to remove",
              true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->removeFile(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// 新: FileCopyTool - implements file.copy
// ============================================================
class FileCopyTool : public Tool {
public:
  explicit FileCopyTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.copy"; }
  std::string description() const override {
    return "Copy a file or directory (recursive), requires user confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.copy",
            "Copy file or directory (recursive, overwrite if exists)",
            {{"source", "string", "relative source path", true, ""},
             {"destination", "string", "relative destination path", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->copyFile(arguments);
  }

private:
  std::shared_ptr<BuiltinShell> shell_;
};

// ============================================================
// 新: FileMoveTool - implements file.move
// ============================================================
class FileMoveTool : public Tool {
public:
  explicit FileMoveTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.move"; }
  std::string description() const override {
    return "Move or rename a file or directory, requires user confirmation";
  }
  std::string group() const override { return ToolGroups::FILE; }

  ToolSchema schema() const override {
    return {"file.move",
            "Move or rename file/directory (requires confirmation)",
            {{"source", "string", "relative source path", true, ""},
             {"destination", "string", "relative destination path", true, ""}}};
  }

  RiskLevel riskLevel(const json &arguments) const override {
    (void)arguments;
    return RiskLevel::Medium;
  }

  ToolResult execute(const ToolContext &context,
                     const json &arguments) override {
    (void)context;
    return shell_->moveFile(arguments);
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

  // Sprint 2.5+ 新工具
  registry.registerTool(std::make_unique<FileSearchTool>(shell));
  registry.registerTool(std::make_unique<FileMkdirTool>(shell));
  registry.registerTool(std::make_unique<FileRmdirTool>(shell));
  registry.registerTool(std::make_unique<FileRemoveTool>(shell));
  registry.registerTool(std::make_unique<FileCopyTool>(shell));
  registry.registerTool(std::make_unique<FileMoveTool>(shell));
}

} // namespace codepilot
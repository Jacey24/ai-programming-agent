#include "BuiltinShell.h"
#include "Tool.h"
#include "ToolRegistry.h"
#include <memory>

namespace codepilot {

// ============================================================
// FileListTool - 实现 file.list
// ============================================================
class FileListTool : public Tool {
public:
  explicit FileListTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.list"; }
  std::string description() const override {
    return "列出工作区内的文件和目录结构，支持递归深度控制";
  }

  ToolSchema schema() const override {
    return {"file.list",
            "列出工作区内的文件和目录结构，支持递归深度控制",
            {{"path", "string", "要列出的目录相对路径（可选，默认根目录）",
              false, ""},
             {"depth", "integer", "递归深度，范围 1-5（可选，默认 2）", false,
              "2"}}};
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
// FileReadTool - 实现 file.read
// ============================================================
class FileReadTool : public Tool {
public:
  explicit FileReadTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.read"; }
  std::string description() const override {
    return "读取工作区内文件的内容，支持指定行范围";
  }

  ToolSchema schema() const override {
    return {"file.read",
            "读取工作区内文件的内容，支持指定行范围",
            {{"path", "string", "文件相对路径", true, ""},
             {"start_line", "integer", "起始行号（可选，默认 1）", false, "1"},
             {"end_line", "integer", "结束行号（可选，默认文件末尾）", false,
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

// ============================================================
// FileWriteTool - 实现 file.write
// ============================================================
class FileWriteTool : public Tool {
public:
  explicit FileWriteTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.write"; }
  std::string description() const override {
    return "写入文件内容（覆盖模式），写入操作需要用户确认";
  }

  ToolSchema schema() const override {
    return {"file.write",
            "写入文件内容（覆盖模式），写入操作需要用户确认",
            {{"path", "string", "文件相对路径", true, ""},
             {"content", "string", "要写入的文件内容", true, ""}}};
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
// FileApplyPatchTool - 实现 file.apply_patch
// ============================================================
class FileApplyPatchTool : public Tool {
public:
  explicit FileApplyPatchTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "file.apply_patch"; }
  std::string description() const override {
    return "应用代码补丁到指定文件，补丁操作需要用户确认";
  }

  ToolSchema schema() const override {
    return {"file.apply_patch",
            "应用代码补丁到指定文件，补丁操作需要用户确认",
            {{"file_path", "string", "要修改的文件相对路径", true, ""},
             {"patch", "string", "补丁内容（diff 格式）", true, ""}}};
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
// ChangeDirTool - 实现 cd
// ============================================================
class ChangeDirTool : public Tool {
public:
  explicit ChangeDirTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "cd"; }
  std::string description() const override { return "切换当前工作目录"; }

  ToolSchema schema() const override {
    return {"cd",
            "切换当前工作目录",
            {{"path", "string", "要切换到的目录相对路径", true, ""}}};
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
// PwdTool - 实现 pwd
// ============================================================
class PwdTool : public Tool {
public:
  explicit PwdTool(std::shared_ptr<BuiltinShell> shell)
      : shell_(std::move(shell)) {}

  std::string name() const override { return "pwd"; }
  std::string description() const override { return "查看当前工作目录路径"; }

  ToolSchema schema() const override {
    return {"pwd", "查看当前工作目录路径", {}};
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
// 工厂函数：向 ToolRegistry 注册所有文件/目录工具
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
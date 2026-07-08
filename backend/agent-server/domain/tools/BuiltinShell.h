#pragma once

#include "Tool.h"
#include "infrastructure/filesystem/Workspace.h"
#include <memory>

namespace codepilot {

// ============================================================
// BuiltinShell - 内部核心 Shell
// 封装所有硬编码内置工具的核心能力
// ============================================================
class BuiltinShell {
public:
  explicit BuiltinShell(std::shared_ptr<Workspace> workspace);

  // --- 文件操作 ---
  ToolResult listFiles(const json &args);
  ToolResult readFile(const json &args);
  ToolResult writeFile(const json &args);
  ToolResult applyPatch(const json &args);

  // --- 目录管理 ---
  ToolResult changeDirectory(const json &args);
  ToolResult getWorkingDirectory(const json &args);

  // --- 新: 文件搜索 ---
  ToolResult searchFiles(const json &args);

  // --- 新: 目录创建/删除 ---
  ToolResult makeDirectory(const json &args);
  ToolResult removeDirectory(const json &args);
  ToolResult removeFile(const json &args);

  // --- 新: 文件复制/移动 ---
  ToolResult copyFile(const json &args);
  ToolResult moveFile(const json &args);

  // --- 路径安全查询（供 RiskDetector 使用） ---
  Workspace *workspace() const { return workspace_.get(); }

  // --- 获取标准化提示词（供 PromptBuilder 使用） ---
  std::string getSystemPrompt() const;

private:
  std::shared_ptr<Workspace> workspace_;
  std::string currentDir_;

  bool applySinglePatch(const std::string &filePath,
                        const std::string &patchContent);
};

} // namespace codepilot
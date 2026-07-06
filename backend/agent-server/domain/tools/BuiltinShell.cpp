#include "BuiltinShell.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace codepilot {

BuiltinShell::BuiltinShell(std::shared_ptr<Workspace> workspace)
    : workspace_(std::move(workspace)), currentDir_("") {}

// ============================================================
// file.list - 列出工作区文件
// ============================================================
ToolResult BuiltinShell::listFiles(const json &args) {
  std::string path =
      args.contains("path") ? args["path"].get<std::string>() : "";
  int depth = args.contains("depth") ? args["depth"].get<int>() : 2;

  if (depth < 1)
    depth = 1;
  if (depth > 5)
    depth = 5;

  auto files = workspace_->listFiles(path, depth);

  std::ostringstream output;
  for (const auto &file : files) {
    output << file.type << "\t" << file.path;
    if (file.type == "file") {
      output << "\t(" << file.size << " bytes)";
    }
    output << "\n";
  }

  int count = static_cast<int>(files.size());
  return ToolResult::Ok("Found " + std::to_string(count) + " entries:\n" +
                        output.str());
}

// ============================================================
// file.read - 读取工作区文件
// ============================================================
ToolResult BuiltinShell::readFile(const json &args) {
  if (!args.contains("path")) {
    return ToolResult::Err("Missing required parameter: path");
  }

  std::string path = args["path"].get<std::string>();
  int startLine =
      args.contains("start_line") ? args["start_line"].get<int>() : 1;
  int endLine = args.contains("end_line") ? args["end_line"].get<int>() : -1;

  if (!workspace_->isPathSafe(path)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + path,
                           -2);
  }

  if (workspace_->isBinaryFile(path)) {
    return ToolResult::Err("Cannot read binary file: " + path, -3);
  }

  int64_t fileSize = workspace_->getFileSize(path);
  if (fileSize < 0) {
    return ToolResult::Err("File not found or cannot be accessed: " + path, -4);
  }
  if (fileSize > 10 * 1024 * 1024) {
    return ToolResult::Err("File too large (max 10MB): " + path, -5);
  }

  std::string content = workspace_->readFile(path, startLine, endLine);
  return ToolResult::Ok(content);
}

// ============================================================
// file.write - 写入文件
// ============================================================
ToolResult BuiltinShell::writeFile(const json &args) {
  if (!args.contains("path") || !args.contains("content")) {
    return ToolResult::Err("Missing required parameters: path, content");
  }

  std::string path = args["path"].get<std::string>();
  std::string content = args["content"].get<std::string>();

  if (!workspace_->isPathSafe(path)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + path,
                           -2);
  }

  std::string fullPath = workspace_->resolvePath(path);
  std::ofstream file(fullPath);
  if (!file.is_open()) {
    return ToolResult::Err("Failed to open file for writing: " + path);
  }

  file << content;
  return ToolResult::Ok("File written successfully: " + path);
}

// ============================================================
// file.apply_patch - 应用代码补丁
// ============================================================
ToolResult BuiltinShell::applyPatch(const json &args) {
  if (!args.contains("file_path") || !args.contains("patch")) {
    return ToolResult::Err("Missing required parameters: file_path, patch");
  }

  std::string filePath = args["file_path"].get<std::string>();
  std::string patch = args["patch"].get<std::string>();

  if (!workspace_->isPathSafe(filePath)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + filePath,
                           -2);
  }

  std::string fullPath = workspace_->resolvePath(filePath);
  if (!applySinglePatch(fullPath, patch)) {
    return ToolResult::Err("Failed to apply patch to: " + filePath);
  }

  return ToolResult::Ok("Patch applied successfully to: " + filePath);
}

// ============================================================
// cd - 切换工作目录
// ============================================================
ToolResult BuiltinShell::changeDirectory(const json &args) {
  if (!args.contains("path")) {
    return ToolResult::Err("Missing required parameter: path");
  }

  std::string path = args["path"].get<std::string>();
  if (workspace_->setCurrentPath(path)) {
    currentDir_ = path;
    return ToolResult::Ok("Changed directory to: " + path);
  }
  return ToolResult::Err("Failed to change directory: " + path);
}

// ============================================================
// pwd - 获取当前工作目录
// ============================================================
ToolResult BuiltinShell::getWorkingDirectory(const json &args) {
  (void)args;
  std::string cwd = currentDir_.empty() ? "/" : currentDir_;
  return ToolResult::Ok(cwd);
}

// ============================================================
// getSystemPrompt - 获取标准化安全提示词
// ============================================================
std::string BuiltinShell::getSystemPrompt() const {
  std::ostringstream prompt;
  prompt << "## 工作区安全规则\n";
  prompt << "- 你只能访问工作区内的文件，工作区根路径："
         << workspace_->rootPath() << "\n";
  prompt << "- 禁止使用 `..` 路径穿越访问工作区外文件\n";
  prompt << "- 禁止访问隐藏文件（以 `.` 开头的文件）\n";
  prompt << "- 禁止读取二进制文件\n";
  prompt << "- 单文件最大读取限制为 10MB\n\n";

  prompt << "## 可用工具\n";
  prompt << "- file.list: 列出工作区文件，参数 path(可选), depth(默认2)\n";
  prompt << "- file.read: 读取文件内容，参数 path(必填), start_line(可选), "
            "end_line(可选)\n";
  prompt << "- file.write: 写入文件，参数 path(必填), content(必填)\n";
  prompt << "- file.apply_patch: 应用代码补丁，参数 file_path(必填), "
            "patch(必填)\n";
  prompt << "- cd: 切换工作目录，参数 path(必填)\n";
  prompt << "- pwd: 查看当前工作目录\n\n";

  prompt << "## 文件写入规则\n";
  prompt << "- 写文件、删除文件、应用补丁均需要用户确认\n";
  prompt << "- 应用补丁前应展示 diff 预览\n";
  return prompt.str();
}

// ============================================================
// applySinglePatch - 简单的补丁应用
// ============================================================
bool BuiltinShell::applySinglePatch(const std::string &filePath,
                                    const std::string &patchContent) {
  std::ifstream inFile(filePath);
  if (!inFile.is_open())
    return false;

  std::stringstream original;
  original << inFile.rdbuf();
  inFile.close();

  std::stringstream originalContent(original.str());
  std::stringstream patchStream(patchContent);

  std::vector<std::string> originalLines;
  std::string line;
  while (std::getline(originalContent, line)) {
    originalLines.push_back(line);
  }

  std::vector<std::string> patchLines;
  while (std::getline(patchStream, line)) {
    patchLines.push_back(line);
  }

  size_t lineIdx = 0;
  for (const auto &pl : patchLines) {
    if (pl.size() > 1 && pl[0] == '-') {
      std::string searchLine = pl.substr(1);
      for (size_t i = lineIdx; i < originalLines.size(); ++i) {
        if (originalLines[i] == searchLine) {
          originalLines[i] = "";
          break;
        }
      }
    } else if (pl.size() > 1 && pl[0] == '+') {
      std::string newLine = pl.substr(1);
      if (lineIdx < originalLines.size()) {
        originalLines.insert(originalLines.begin() + lineIdx, newLine);
        ++lineIdx;
      }
    } else if (pl.size() > 4 && pl[0] == '@' && pl[1] == '@') {
      continue;
    }
    ++lineIdx;
  }

  std::ofstream outFile(filePath);
  if (!outFile.is_open())
    return false;

  for (const auto &l : originalLines) {
    if (!l.empty()) {
      outFile << l << "\n";
    }
  }

  return true;
}

} // namespace codepilot
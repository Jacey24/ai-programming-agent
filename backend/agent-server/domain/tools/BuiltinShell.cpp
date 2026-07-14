#include "BuiltinShell.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace codepilot {

// Windows MSVC: filesystem::path::string() returns ANSI (GBK) encoding.
// To ensure valid UTF-8 in JSON output, we must use u8string() instead.
static std::string path_to_utf8(const std::filesystem::path &p) {
  auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

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
// 新: file.search - 在工作区中搜索文件内容
// ============================================================
ToolResult BuiltinShell::searchFiles(const json &args) {
  if (!args.contains("pattern")) {
    return ToolResult::Err("Missing required parameter: pattern");
  }

  std::string pattern = args["pattern"].get<std::string>();
  std::string root =
      args.contains("root") ? args["root"].get<std::string>() : "";

  if (pattern.empty()) {
    return ToolResult::Err("Search pattern cannot be empty");
  }

  // 安全检查
  if (!root.empty() && !workspace_->isPathSafe(root)) {
    return ToolResult::Err(
        "Search root is not safe or outside workspace: " + root, -2);
  }

  std::string searchRoot = workspace_->resolvePath(root);
  std::regex searchRegex(pattern, std::regex::icase);

  std::ostringstream output;
  int matchCount = 0;
  int fileCount = 0;

  try {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(searchRoot)) {
      if (!entry.is_regular_file())
        continue;

      std::string filePath = path_to_utf8(entry.path().filename());

      // 跳过隐藏文件、二进制和常见忽略目录
      if (filePath[0] == '.')
        continue;
      std::string ext = path_to_utf8(entry.path().extension());
      if (ext == ".exe" || ext == ".dll" || ext == ".obj" || ext == ".o" ||
          ext == ".bin")
        continue;

      // 检查是否在忽略目录中
      std::string parent = path_to_utf8(entry.path().parent_path());
      bool ignored = false;
      for (const auto &ignoredDir : {
               "build",
               ".git",
               "node_modules",
               ".venv",
               "__pycache__",
               ".cache",
           }) {
        if (parent.find(ignoredDir) != std::string::npos) {
          ignored = true;
          break;
        }
      }
      if (ignored)
        continue;

      // 文件大小限制（5MB）
      auto fsPath = entry.path();
      std::error_code ec;
      auto fileSize = std::filesystem::file_size(fsPath, ec);
      if (ec || fileSize > 5 * 1024 * 1024)
        continue;

      std::ifstream file(fsPath);
      if (!file.is_open())
        continue;

      std::string line;
      int lineNum = 0;
      bool fileHasMatch = false;

      while (std::getline(file, line)) {
        lineNum++;
        try {
          if (std::regex_search(line, searchRegex)) {
            if (!fileHasMatch) {
              output << path_to_utf8(entry.path().lexically_relative(
                            std::filesystem::path(searchRoot)))
                     << ":\n";
              fileHasMatch = true;
              fileCount++;
            }
            output << "  " << lineNum << ": " << line << "\n";
            matchCount++;
          }
        } catch (const std::regex_error &) {
          return ToolResult::Err("Invalid regex pattern: " + pattern, -6);
        }
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    return ToolResult::Err("Filesystem error during search: " +
                           std::string(e.what()));
  }

  std::ostringstream summary;
  summary << "Search results for \"" << pattern << "\":\n";
  summary << "  Matched " << matchCount << " lines in " << fileCount
          << " files\n";
  if (matchCount > 0) {
    summary << "\n" << output.str();
  }

  return ToolResult::Ok(summary.str());
}

// ============================================================
// 新: file.mkdir - 创建目录
// ============================================================
ToolResult BuiltinShell::makeDirectory(const json &args) {
  if (!args.contains("path")) {
    return ToolResult::Err("Missing required parameter: path");
  }

  std::string path = args["path"].get<std::string>();
  if (!workspace_->isPathSafe(path)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + path,
                           -2);
  }

  std::string fullPath = workspace_->resolvePath(path);
  std::error_code ec;
  if (std::filesystem::create_directories(fullPath, ec)) {
    return ToolResult::Ok("Directory created successfully: " + path);
  } else if (ec) {
    return ToolResult::Err("Failed to create directory: " + path + " (" +
                           ec.message() + ")");
  } else {
    return ToolResult::Ok("Directory already exists: " + path);
  }
}

// ============================================================
// 新: file.rmdir - 删除空目录
// ============================================================
ToolResult BuiltinShell::removeDirectory(const json &args) {
  if (!args.contains("path")) {
    return ToolResult::Err("Missing required parameter: path");
  }

  std::string path = args["path"].get<std::string>();
  if (!workspace_->isPathSafe(path)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + path,
                           -2);
  }

  std::string fullPath = workspace_->resolvePath(path);
  if (!std::filesystem::exists(fullPath)) {
    return ToolResult::Err("Directory not found: " + path, -4);
  }
  if (!std::filesystem::is_directory(fullPath)) {
    return ToolResult::Err("Not a directory: " + path, -7);
  }

  std::error_code ec;
  if (std::filesystem::remove(fullPath, ec)) {
    return ToolResult::Ok("Directory removed successfully: " + path);
  } else if (ec) {
    return ToolResult::Err("Failed to remove directory: " + path + " (" +
                           ec.message() + ")");
  } else {
    return ToolResult::Err("Directory not empty or could not be removed: " +
                           path);
  }
}

// ============================================================
// 新: file.remove - 删除文件或递归删除目录
// ============================================================
ToolResult BuiltinShell::removeFile(const json &args) {
  if (!args.contains("path")) {
    return ToolResult::Err("Missing required parameter: path");
  }

  std::string path = args["path"].get<std::string>();
  if (!workspace_->isPathSafe(path)) {
    return ToolResult::Err("Path is not safe or outside workspace: " + path,
                           -2);
  }

  std::string fullPath = workspace_->resolvePath(path);
  if (!std::filesystem::exists(fullPath)) {
    return ToolResult::Err("Path not found: " + path, -4);
  }

  std::error_code ec;
  uintmax_t count = std::filesystem::remove_all(fullPath, ec);
  if (ec) {
    return ToolResult::Err("Failed to remove: " + path + " (" + ec.message() +
                           ")");
  }
  return ToolResult::Ok("Removed " + std::to_string(count) +
                        " item(s): " + path);
}

// ============================================================
// 新: file.copy - 复制文件或目录
// ============================================================
ToolResult BuiltinShell::copyFile(const json &args) {
  if (!args.contains("source") || !args.contains("destination")) {
    return ToolResult::Err("Missing required parameters: source, destination");
  }

  std::string source = args["source"].get<std::string>();
  std::string dest = args["destination"].get<std::string>();

  if (!workspace_->isPathSafe(source)) {
    return ToolResult::Err("Source path is not safe: " + source, -2);
  }
  if (!workspace_->isPathSafe(dest)) {
    return ToolResult::Err("Destination path is not safe: " + dest, -2);
  }

  std::string fullSource = workspace_->resolvePath(source);
  std::string fullDest = workspace_->resolvePath(dest);

  if (!std::filesystem::exists(fullSource)) {
    return ToolResult::Err("Source not found: " + source, -4);
  }

  std::error_code ec;
  std::filesystem::copy_options opts =
      std::filesystem::copy_options::recursive |
      std::filesystem::copy_options::overwrite_existing;

  std::filesystem::copy(fullSource, fullDest, opts, ec);
  if (ec) {
    return ToolResult::Err("Failed to copy: " + source + " -> " + dest + " (" +
                           ec.message() + ")");
  }
  return ToolResult::Ok("Copied successfully: " + source + " -> " + dest);
}

// ============================================================
// 新: file.move - 移动文件或目录
// ============================================================
ToolResult BuiltinShell::moveFile(const json &args) {
  if (!args.contains("source") || !args.contains("destination")) {
    return ToolResult::Err("Missing required parameters: source, destination");
  }

  std::string source = args["source"].get<std::string>();
  std::string dest = args["destination"].get<std::string>();

  if (!workspace_->isPathSafe(source)) {
    return ToolResult::Err("Source path is not safe: " + source, -2);
  }
  if (!workspace_->isPathSafe(dest)) {
    return ToolResult::Err("Destination path is not safe: " + dest, -2);
  }

  std::string fullSource = workspace_->resolvePath(source);
  std::string fullDest = workspace_->resolvePath(dest);

  if (!std::filesystem::exists(fullSource)) {
    return ToolResult::Err("Source not found: " + source, -4);
  }

  std::error_code ec;
  std::filesystem::rename(fullSource, fullDest, ec);
  if (ec) {
    return ToolResult::Err("Failed to move: " + source + " -> " + dest + " (" +
                           ec.message() + ")");
  }
  return ToolResult::Ok("Moved successfully: " + source + " -> " + dest);
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

  prompt << "## 可用工具\n\n";
  prompt << "### 文件操作\n";
  prompt << "- file.list: 列出工作区文件，参数 path(可选), depth(默认2)\n";
  prompt << "- file.read: 读取文件内容，参数 path(必填), start_line(可选), "
            "end_line(可选)\n";
  prompt << "- file.write: 写入文件，参数 path(必填), content(必填)\n";
  prompt << "- file.search: 搜索文件内容，参数 pattern(必填), root(可选)\n";
  prompt << "- file.mkdir: 创建目录，参数 path(必填)\n";
  prompt << "- file.rmdir: 删除空目录，参数 path(必填)\n";
  prompt << "- file.remove: 删除文件或目录，参数 path(必填)\n";
  prompt << "- file.copy: 复制文件或目录，参数 source(必填), "
            "destination(必填)\n";
  prompt << "- file.move: 移动文件或目录，参数 source(必填), "
            "destination(必填)\n";
  prompt << "- file.apply_patch: 应用代码补丁，参数 file_path(必填), "
            "patch(必填)\n";
  prompt << "- cd: 切换工作目录，参数 path(必填)\n";
  prompt << "- pwd: 查看当前工作目录\n\n";
  prompt << "### Shell 命令（Windows: cmd.exe / Unix: bash）\n";
  prompt << "- shell.run: 在工作区执行命令，参数 command(必填), cwd(可选), "
            "timeout(可选，默认60秒)\n";
  prompt << "  高危险命令（如 rm -rf /、del /F /S）被系统阻止。"
            "中危命令需用户确认后执行。\n\n";
  prompt << "### Git 版本控制（需安装 Git）\n";
  prompt << "- git.status: 查看仓库状态\n";
  prompt << "- git.diff: 查看文件变更，参数 path(可选)\n";
  prompt
      << "- git.commit: 暂存并提交所有变更，参数 message(必填) — 需用户确认\n";
  prompt << "- git.log: 查看提交历史，参数 count(可选，默认20)\n";
  prompt << "- git.branch: 查看或创建分支，参数 action(可选: list/create), "
            "name(可选)\n\n";

  prompt << "## 文件写入规则\n";
  prompt << "- 写文件、删除文件、删除目录、复制、移动均需要用户确认\n";
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

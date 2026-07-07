#pragma once

#include <filesystem>
#include <string>
#include <vector>


namespace codepilot {

// ============================================================
// 文件条目信息
// ============================================================
struct FileEntry {
  std::string name;
  std::string path; // 相对于 workspace 的路径
  std::string type; // "file" 或 "directory"
  int64_t size{0};
  std::string lastModified;
};

// ============================================================
// Workspace - 工作区路径安全管理器
// 负责：路径校验、文件列目录、文件读取
// ============================================================
class Workspace {
public:
  explicit Workspace(const std::string &rootPath);

  // --- 路径查询 ---
  const std::string &rootPath() const { return rootPath_; }
  std::string currentPath() const { return currentPath_; }

  // --- 工作目录管理 ---
  bool setCurrentPath(const std::string &relativePath);
  std::string resolvePath(const std::string &relativePath) const;

  // --- 路径安全校验 ---
  bool isPathSafe(const std::string &relativePath) const;
  bool isPathWithinWorkspace(const std::string &absolutePath) const;

  // --- 文件操作 ---
  std::vector<FileEntry> listFiles(const std::string &relativePath,
                                   int depth = 1) const;
  std::string readFile(const std::string &relativePath, int startLine = 1,
                       int endLine = -1) const;
  int64_t getFileSize(const std::string &relativePath) const;
  bool isBinaryFile(const std::string &relativePath) const;

  // --- 默认忽略目录 ---
  static const std::vector<std::string> &defaultIgnoredDirs();

private:
  std::string rootPath_;
  std::string currentPath_;

  // 规范化路径（解析 . 和 ..）
  std::string normalizePath(const std::string &path) const;

  // 递归列举文件
  void listFilesRecursive(const std::filesystem::path &dirPath,
                          const std::string &relativePrefix,
                          std::vector<FileEntry> &results,
                          int remainingDepth) const;

  // 检查是否在忽略列表中
  bool isIgnored(const std::string &name) const;
};

} // namespace codepilot
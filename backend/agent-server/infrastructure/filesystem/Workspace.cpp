#include "Workspace.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>


namespace codepilot {

static const std::vector<std::string> kDefaultIgnoredDirs = {
    ".git",   "build",  "dist",  "node_modules",
    "target", ".cache", ".idea", ".vscode"};

const std::vector<std::string> &Workspace::defaultIgnoredDirs() {
  return kDefaultIgnoredDirs;
}

Workspace::Workspace(const std::string &rootPath)
    : rootPath_(normalizePath(rootPath)),
      currentPath_("/") // 相对于 rootPath 的路径，默认根
{
  // 确保 rootPath 结尾没有 /
  if (!rootPath_.empty() && rootPath_.back() == '/') {
    rootPath_.pop_back();
  }
}

std::string Workspace::normalizePath(const std::string &path) const {
  std::filesystem::path p(path);
  try {
    return std::filesystem::weakly_canonical(p).string();
  } catch (...) {
    // 如果路径不存在，fallback 到 lexical 规范化
    return p.lexically_normal().string();
  }
}

bool Workspace::isPathSafe(const std::string &relativePath) const {
  // 1. 禁止路径穿越 (..)
  if (relativePath.find("..") != std::string::npos) {
    return false;
  }

  // 2. 绝对路径直接拒绝
  if (!relativePath.empty() && relativePath[0] == '/') {
    return false;
  }

  // 3. 禁止访问隐藏文件（以 . 开头）
  std::filesystem::path p(relativePath);
  for (const auto &part : p) {
    std::string s = part.string();
    if (!s.empty() && s[0] == '.' && s != "." && s != "..") {
      return false;
    }
  }

  // 4. 解析完整路径并在 workspace 内
  std::string fullPath = resolvePath(relativePath);
  return isPathWithinWorkspace(fullPath);
}

std::string Workspace::resolvePath(const std::string &relativePath) const {
  if (relativePath.empty()) {
    return rootPath_;
  }

  // 组合根路径和相对路径
  std::filesystem::path full = std::filesystem::path(rootPath_) / relativePath;
  try {
    return std::filesystem::weakly_canonical(full).string();
  } catch (...) {
    return full.lexically_normal().string();
  }
}

bool Workspace::isPathWithinWorkspace(const std::string &absolutePath) const {
  // 规范化两个路径
  std::string normAbs = normalizePath(absolutePath);
  std::string normRoot = normalizePath(rootPath_);

  // 检查 absolutePath 是否以 rootPath 开头
  if (normAbs.size() < normRoot.size()) {
    return false;
  }
  if (normAbs.compare(0, normRoot.size(), normRoot) != 0) {
    return false;
  }
  // 确保是精确匹配或后面有 /
  if (normAbs.size() > normRoot.size() && normAbs[normRoot.size()] != '/' &&
      normAbs[normRoot.size()] != '\\') {
    return false;
  }
  return true;
}

bool Workspace::setCurrentPath(const std::string &relativePath) {
  if (!isPathSafe(relativePath)) {
    return false;
  }

  std::string fullPath = resolvePath(relativePath);
  if (!std::filesystem::exists(fullPath) ||
      !std::filesystem::is_directory(fullPath)) {
    return false;
  }

  // 计算相对于 root 的路径
  currentPath_ = "/" + relativePath;
  // 规范化多余 /
  while (currentPath_.find("//") != std::string::npos) {
    currentPath_.replace(currentPath_.find("//"), 2, "/");
  }
  return true;
}

std::vector<FileEntry> Workspace::listFiles(const std::string &relativePath,
                                            int depth) const {
  std::vector<FileEntry> results;

  // 检查路径安全
  if (relativePath.empty()) {
    // 列根目录
    std::string fullPath = rootPath_;
    if (std::filesystem::exists(fullPath) &&
        std::filesystem::is_directory(fullPath)) {
      listFilesRecursive(std::filesystem::path(fullPath), "", results, depth);
    }
  } else {
    if (!isPathSafe(relativePath)) {
      return results;
    }
    std::string fullPath = resolvePath(relativePath);
    if (std::filesystem::exists(fullPath) &&
        std::filesystem::is_directory(fullPath)) {
      listFilesRecursive(std::filesystem::path(fullPath), relativePath + "/",
                         results, depth);
    }
  }

  return results;
}

void Workspace::listFilesRecursive(const std::filesystem::path &dirPath,
                                   const std::string &relativePrefix,
                                   std::vector<FileEntry> &results,
                                   int remainingDepth) const {
  if (remainingDepth <= 0)
    return;

  try {
    for (const auto &entry : std::filesystem::directory_iterator(dirPath)) {
      std::string name = entry.path().filename().string();

      // 跳过忽略目录
      if (isIgnored(name))
        continue;

      FileEntry fileEntry;
      fileEntry.name = name;
      fileEntry.path = relativePrefix + name;

      if (entry.is_directory()) {
        fileEntry.type = "directory";
        results.push_back(fileEntry);
        // 递归子目录
        listFilesRecursive(entry.path(), relativePrefix + name + "/", results,
                           remainingDepth - 1);
      } else if (entry.is_regular_file()) {
        fileEntry.type = "file";
        fileEntry.size = entry.file_size();
        results.push_back(fileEntry);
      }
    }
  } catch (...) {
    // 忽略权限错误等
  }
}

std::string Workspace::readFile(const std::string &relativePath, int startLine,
                                int endLine) const {
  if (!isPathSafe(relativePath)) {
    return "";
  }

  // 检查是否是二进制文件
  if (isBinaryFile(relativePath)) {
    return ""; // 二进制文件不读取
  }

  std::string fullPath = resolvePath(relativePath);
  std::ifstream file(fullPath);
  if (!file.is_open()) {
    return "";
  }

  // 检查文件大小（限制 10MB）
  auto size = std::filesystem::file_size(fullPath);
  if (size > 10 * 1024 * 1024) {
    return ""; // 文件太大
  }

  std::string line;
  int currentLine = 0;
  std::ostringstream result;

  while (std::getline(file, line)) {
    currentLine++;
    if (currentLine < startLine)
      continue;
    if (endLine > 0 && currentLine > endLine)
      break;
    result << line << "\n";
  }

  return result.str();
}

int64_t Workspace::getFileSize(const std::string &relativePath) const {
  if (!isPathSafe(relativePath))
    return -1;
  std::string fullPath = resolvePath(relativePath);
  try {
    if (std::filesystem::exists(fullPath)) {
      return std::filesystem::file_size(fullPath);
    }
  } catch (...) {
  }
  return -1;
}

bool Workspace::isBinaryFile(const std::string &relativePath) const {
  if (!isPathSafe(relativePath))
    return true;
  std::string fullPath = resolvePath(relativePath);
  std::ifstream file(fullPath, std::ios::binary);
  if (!file.is_open())
    return true;

  // 读取前 512 字节检查是否有 null 字符
  char buffer[512];
  file.read(buffer, sizeof(buffer));
  std::streamsize bytesRead = file.gcount();

  for (std::streamsize i = 0; i < bytesRead; ++i) {
    if (buffer[i] == '\0') {
      return true; // 包含 null 字符，视为二进制
    }
  }
  return false;
}

bool Workspace::isIgnored(const std::string &name) const {
  for (const auto &ignored : kDefaultIgnoredDirs) {
    if (name == ignored)
      return true;
  }
  return false;
}

} // namespace codepilot
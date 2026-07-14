#include "WorkspaceRuntime.h"

#include <filesystem>

namespace codepilot {

// static
std::shared_ptr<WorkspaceRuntime>
WorkspaceRuntime::create(const std::string &id, const std::string &path) {
  auto rt = std::make_shared<WorkspaceRuntime>();
  rt->workspaceId = id;
  rt->workspacePath = path;

  // 创建 Workspace 实例
  rt->workspace = std::make_shared<Workspace>(path);
  rt->builtinShell = std::make_shared<BuiltinShell>(rt->workspace);

  // 创建 ProcessRunner 并设置默认 CWD 为 workspace 根目录
  rt->processRunner = std::make_shared<ProcessRunner>();
  rt->processRunner->setWorkingDirectory(path);

  // 创建该 workspace 独立的 ToolRegistry
  rt->toolRegistry = std::make_shared<ToolRegistry>();

  return rt;
}

bool WorkspaceRuntime::relocate(const std::string &newPath) {
  if (newPath.empty()) {
    return false;
  }

  // 验证新路径存在且为目录
  std::error_code ec;
  auto canonical = std::filesystem::weakly_canonical(newPath, ec);
  if (ec || !std::filesystem::exists(canonical) ||
      !std::filesystem::is_directory(canonical)) {
    return false;
  }

  workspacePath = canonical.string();

  // 重建 Workspace
  workspace = std::make_shared<Workspace>(workspacePath);
  builtinShell = std::make_shared<BuiltinShell>(workspace);

  // 更新 ProcessRunner CWD
  processRunner->setWorkingDirectory(workspacePath);

  return true;
}

} // namespace codepilot

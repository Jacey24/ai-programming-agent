#include "WorkspaceManager.h"

#include <shared_mutex>

namespace codepilot {

// static
WorkspaceManager &WorkspaceManager::getInstance() {
  static WorkspaceManager instance;
  return instance;
}

std::shared_ptr<WorkspaceRuntime>
WorkspaceManager::getOrCreate(const std::string &workspaceId,
                              const std::string &path) {
  {
    // 先尝试读锁查找
    std::shared_lock lock(mutex_);
    auto it = runtimes_.find(workspaceId);
    if (it != runtimes_.end()) {
      if (!path.empty()) {
        std::lock_guard runtimeLock(it->second->executionMutex);
        if (path != it->second->workspacePath) {
          it->second->relocate(path);
        }
      }
      return it->second;
    }
  }

  // 需要创建，使用写锁
  std::unique_lock lock(mutex_);

  // 双重检查（防止多线程同时创建）
  auto it = runtimes_.find(workspaceId);
  if (it != runtimes_.end()) {
    if (!path.empty()) {
      std::lock_guard runtimeLock(it->second->executionMutex);
      if (path != it->second->workspacePath) {
        it->second->relocate(path);
      }
    }
    return it->second;
  }

  auto rt = WorkspaceRuntime::create(workspaceId, path);
  runtimes_[workspaceId] = rt;
  return rt;
}

std::shared_ptr<WorkspaceRuntime>
WorkspaceManager::get(const std::string &workspaceId) const {
  std::shared_lock lock(mutex_);
  auto it = runtimes_.find(workspaceId);
  if (it != runtimes_.end()) {
    return it->second;
  }
  return nullptr;
}

void WorkspaceManager::invalidate(const std::string &workspaceId) {
  std::unique_lock lock(mutex_);
  runtimes_.erase(workspaceId);
}

bool WorkspaceManager::has(const std::string &workspaceId) const {
  std::shared_lock lock(mutex_);
  return runtimes_.find(workspaceId) != runtimes_.end();
}

std::vector<std::shared_ptr<WorkspaceRuntime>>
WorkspaceManager::getAll() const {
  std::shared_lock lock(mutex_);
  std::vector<std::shared_ptr<WorkspaceRuntime>> result;
  result.reserve(runtimes_.size());
  for (const auto &[id, rt] : runtimes_) {
    result.push_back(rt);
  }
  return result;
}

std::vector<std::string> WorkspaceManager::listIds() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> result;
  result.reserve(runtimes_.size());
  for (const auto &[id, rt] : runtimes_) {
    result.push_back(id);
  }
  return result;
}

void WorkspaceManager::clear() {
  std::unique_lock lock(mutex_);
  runtimes_.clear();
}

} // namespace codepilot

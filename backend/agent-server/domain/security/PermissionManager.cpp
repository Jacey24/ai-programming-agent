#include "PermissionManager.h"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace codepilot {

// ============================================================
// PermissionRequest 实现
// ============================================================
std::string PermissionRequest::statusToString() const {
  switch (status) {
  case PermissionStatus::Pending:
    return "pending";
  case PermissionStatus::Approved:
    return "approved";
  case PermissionStatus::Rejected:
    return "rejected";
  case PermissionStatus::Expired:
    return "expired";
  default:
    return "unknown";
  }
}

// ============================================================
// 辅助函数：生成 ISO 8601 时间
// ============================================================
static std::string permTimestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// ============================================================
// PermissionManager 实现
// ============================================================
PermissionManager::PermissionManager(std::shared_ptr<EventBus> eventBus)
    : eventBus_(eventBus) {}

bool PermissionManager::requiresPermission(const std::string & /*toolName*/,
                                           RiskLevel riskLevel,
                                           const json & /*arguments*/) const {
  // Safe → 不需要权限
  // Medium/Dangerous → 需要用户确认
  // Blocked → 直接拒绝（不在权限管理范围，由 RiskDetector 标记）
  return riskLevel == RiskLevel::Medium || riskLevel == RiskLevel::Dangerous;
}

std::string PermissionManager::generateId() {
  return "perm_" + std::to_string(nextId_++);
}

std::string PermissionManager::currentTimestamp() { return permTimestamp(); }

PermissionRequest PermissionManager::createRequest(const std::string &taskId,
                                                   const std::string &toolName,
                                                   RiskLevel riskLevel,
                                                   const json &arguments) {
  PermissionRequest req;
  req.id = generateId();
  req.taskId = taskId;
  req.toolName = toolName;
  req.arguments = arguments;
  req.riskLevel = riskLevel;
  req.status = PermissionStatus::Pending;
  req.createdAt = currentTimestamp();

  {
    std::lock_guard<std::mutex> lock(requestsMutex_);
    requests_[req.id] = req;
  }

  // ★ 自动创建等待器
  {
    std::lock_guard<std::mutex> lock(waitersMutex_);
    waiters_[req.id] = std::make_shared<PendingWaiter>();
  }

  // 发布权限请求事件
  json meta;
  meta["tool_name"] = toolName;
  meta["risk_level"] = riskLevelToString(riskLevel);
  meta["arguments"] = arguments;
  meta["permission_id"] = req.id;
  meta["request_id"] = req.id;

  if (eventBus_) {
    eventBus_->publish(EventData::Create(taskId, EventType::PermissionRequired,
                                         "需要权限确认: " + toolName, meta));
  }

  return req;
}

// ============================================================
// ★ 新增：waitForResolution — 同步等待用户决策
// ============================================================
bool PermissionManager::waitForResolution(const std::string &requestId,
                                          int timeoutSeconds) {
  std::shared_ptr<PendingWaiter> waiter;

  // 获取等待器
  {
    std::lock_guard<std::mutex> lock(waitersMutex_);
    auto it = waiters_.find(requestId);
    if (it == waiters_.end()) {
      return false; // 请求不存在
    }
    waiter = it->second;
  }

  // 阻塞等待条件变量，直到被 resolvePermission 唤醒或超时
  std::unique_lock<std::mutex> lock(waiter->mtx);
  bool notified =
      waiter->cv.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                          [waiter]() { return waiter->resolved; });

  if (!notified) {
    // 超时：标记请求为过期
    expire(requestId);
    // 移除等待器
    {
      std::lock_guard<std::mutex> wl(waitersMutex_);
      waiters_.erase(requestId);
    }
    return false;
  }

  bool result = waiter->approved;

  // 清理等待器（已经 resolved 了）
  {
    std::lock_guard<std::mutex> wl(waitersMutex_);
    waiters_.erase(requestId);
  }

  return result;
}

// ============================================================
// ★ 新增：resolvePermission — API 层回调入口
// ============================================================
bool PermissionManager::resolvePermission(const std::string &requestId,
                                          bool approved) {
  // 更新请求状态（在锁内完成，并拷贝发布事件所需字段）
  std::string toolName;
  std::string taskId;
  {
    std::lock_guard<std::mutex> lock(requestsMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end() ||
        it->second.status != PermissionStatus::Pending) {
      return false;
    }
    it->second.status =
        approved ? PermissionStatus::Approved : PermissionStatus::Rejected;
    it->second.resolvedAt = currentTimestamp();
    toolName = it->second.toolName;
    taskId = it->second.taskId;
  }

  // 通知等待的线程
  std::shared_ptr<PendingWaiter> waiter;
  {
    std::lock_guard<std::mutex> lock(waitersMutex_);
    auto wit = waiters_.find(requestId);
    if (wit != waiters_.end()) {
      waiter = wit->second;
    }
  }

  if (waiter) {
    std::lock_guard<std::mutex> lock(waiter->mtx);
    waiter->resolved = true;
    waiter->approved = approved;
    waiter->cv.notify_all();
  }

  // 发布权限解析事件
  json meta;
  meta["request_id"] = requestId;
  meta["tool_name"] = toolName;
  meta["approved"] = approved;

  if (eventBus_) {
    eventBus_->publish(
        EventData::Create(taskId, EventType::PermissionResolved,
                          approved ? "权限已批准" : "权限已拒绝", meta));
  }

  return true;
}

// ============================================================
// approve / reject — 兼容旧接口，内部调用 resolvePermission
// ============================================================
bool PermissionManager::approve(const std::string &requestId) {
  return resolvePermission(requestId, true);
}

bool PermissionManager::reject(const std::string &requestId) {
  return resolvePermission(requestId, false);
}

void PermissionManager::expire(const std::string &requestId) {
  {
    std::lock_guard<std::mutex> lock(requestsMutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end()) {
      return;
    }
    it->second.status = PermissionStatus::Expired;
    it->second.resolvedAt = currentTimestamp();
  }

  // 如果有人在等待，也通知他们（false = 拒绝/超时）
  std::shared_ptr<PendingWaiter> waiter;
  {
    std::lock_guard<std::mutex> lock(waitersMutex_);
    auto wit = waiters_.find(requestId);
    if (wit != waiters_.end()) {
      waiter = wit->second;
    }
  }

  if (waiter) {
    std::lock_guard<std::mutex> lock(waiter->mtx);
    waiter->resolved = true;
    waiter->approved = false;
    waiter->cv.notify_all();
  }
}

PermissionRequest *PermissionManager::getRequest(const std::string &requestId) {
  auto it = requests_.find(requestId);
  if (it == requests_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<PermissionRequest>
PermissionManager::getPendingRequests(const std::string &taskId) const {
  std::vector<PermissionRequest> result;
  std::lock_guard<std::mutex> lock(requestsMutex_);
  for (const auto &[id, req] : requests_) {
    if (req.status != PermissionStatus::Pending) {
      continue;
    }
    if (taskId.empty() || req.taskId == taskId) {
      result.push_back(req);
    }
  }
  return result;
}

std::optional<PermissionRequest>
PermissionManager::getRequestCopy(const std::string &requestId) const {
  std::lock_guard<std::mutex> lock(requestsMutex_);
  auto it = requests_.find(requestId);
  if (it == requests_.end()) {
    return std::nullopt;
  }
  return it->second;
}

} // namespace codepilot

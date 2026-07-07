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
  gmtime_s(&tm, &t);
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

  requests_[req.id] = req;

  // 发布权限请求事件
  json meta;
  meta["tool_name"] = toolName;
  meta["risk_level"] = riskLevelToString(riskLevel);
  meta["arguments"] = arguments;
  meta["request_id"] = req.id;

  if (eventBus_) {
    eventBus_->publish(EventData::Create(taskId, EventType::PermissionRequired,
                                         "需要权限确认: " + toolName, meta));
  }

  return req;
}

bool PermissionManager::approve(const std::string &requestId) {
  auto it = requests_.find(requestId);
  if (it == requests_.end() || it->second.status != PermissionStatus::Pending) {
    return false;
  }

  it->second.status = PermissionStatus::Approved;
  it->second.resolvedAt = currentTimestamp();

  json meta;
  meta["request_id"] = requestId;
  meta["tool_name"] = it->second.toolName;
  meta["approved"] = true;

  if (eventBus_) {
    eventBus_->publish(EventData::Create(
        it->second.taskId, EventType::PermissionResolved, "权限已批准", meta));
  }

  return true;
}

bool PermissionManager::reject(const std::string &requestId) {
  auto it = requests_.find(requestId);
  if (it == requests_.end() || it->second.status != PermissionStatus::Pending) {
    return false;
  }

  it->second.status = PermissionStatus::Rejected;
  it->second.resolvedAt = currentTimestamp();

  json meta;
  meta["request_id"] = requestId;
  meta["tool_name"] = it->second.toolName;
  meta["approved"] = false;

  if (eventBus_) {
    eventBus_->publish(EventData::Create(
        it->second.taskId, EventType::PermissionResolved, "权限已拒绝", meta));
  }

  return true;
}

void PermissionManager::expire(const std::string &requestId) {
  auto it = requests_.find(requestId);
  if (it == requests_.end()) {
    return;
  }

  it->second.status = PermissionStatus::Expired;
  it->second.resolvedAt = currentTimestamp();
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

} // namespace codepilot
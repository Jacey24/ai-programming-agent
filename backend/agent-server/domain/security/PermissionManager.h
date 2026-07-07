#pragma once

#include "RiskDetector.h"
#include "domain/tools/Tool.h"
#include "event/EventBus.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// 权限请求状态
// 对齐整体架构说明.md 第7.4节
// ============================================================
enum class PermissionStatus { Pending, Approved, Rejected, Expired };

struct PermissionRequest {
  std::string id;
  std::string taskId;
  std::string toolName;
  json arguments;
  RiskLevel riskLevel{RiskLevel::Safe};
  PermissionStatus status{PermissionStatus::Pending};
  std::string createdAt;
  std::string resolvedAt;

  std::string statusToString() const;
};

// ============================================================
// PermissionManager — 权限管理器
//
// 职责：
//   1. 检查工具调用是否需要用户确认
//   2. 创建权限请求
//   3. 处理用户批准/拒绝
//   4. 发布权限相关事件
//
// 对齐整体架构说明.md 第10.2节
// ============================================================
class PermissionManager {
public:
  explicit PermissionManager(std::shared_ptr<EventBus> eventBus);

  // --- 检查工具调用是否需要权限 ---
  // 返回是否需要等待用户确认
  bool requiresPermission(const std::string &toolName, RiskLevel riskLevel,
                          const json &arguments) const;

  // --- 创建权限请求 ---
  PermissionRequest createRequest(const std::string &taskId,
                                  const std::string &toolName,
                                  RiskLevel riskLevel, const json &arguments);

  // --- 处理用户响应 ---
  bool approve(const std::string &requestId);
  bool reject(const std::string &requestId);

  // --- 使过期 ---
  void expire(const std::string &requestId);

  // --- 查询请求 ---
  PermissionRequest *getRequest(const std::string &requestId);
  std::vector<PermissionRequest>
  getPendingRequests(const std::string &taskId = "") const;

private:
  std::shared_ptr<EventBus> eventBus_;
  std::unordered_map<std::string, PermissionRequest> requests_;
  size_t nextId_{1};

  std::string generateId();
  std::string currentTimestamp();
};

} // namespace codepilot
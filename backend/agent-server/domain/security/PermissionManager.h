#pragma once

#include "RiskDetector.h"
#include "domain/tools/Tool.h"
#include "event/EventBus.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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
//   3. **同步等待用户决策**（阻塞 Agent 直至用户点击同意/拒绝）
//   4. 处理用户批准/拒绝（由 API 层回调触发）
//   5. 发布权限相关事件
//
// 对齐整体架构说明.md 第10.2节
//
// ★ Sprint 2 新增：waitForResolution / resolvePermission 机制
//    - createRequest 后，Agent 调用 waitForResolution 阻塞等待
//    - 郑嘉娴的 API Controller 调用 resolvePermission() 来唤醒
//    - 不依赖外部代码，纯内部 std::condition_variable 实现
// ============================================================
class PermissionManager {
public:
  explicit PermissionManager(std::shared_ptr<EventBus> eventBus);

  // --- 检查工具调用是否需要权限 ---
  // 返回是否需要等待用户确认
  bool requiresPermission(const std::string &toolName, RiskLevel riskLevel,
                          const json &arguments) const;

  // --- 创建权限请求（非阻塞） ---
  // 创建后调用 waitForResolution() 进入等待
  PermissionRequest createRequest(const std::string &taskId,
                                  const std::string &toolName,
                                  RiskLevel riskLevel, const json &arguments);

  // --- ★ 新增：同步等待用户决策 ---
  // 阻塞当前线程直到用户同意/拒绝，或超时
  // 参数: requestId - createRequest 返回的 id
  //       timeoutSeconds - 超时秒数（默认 120s）
  // 返回: true = 用户同意, false = 用户拒绝/超时/请求不存在
  // 线程安全：内部使用 condition_variable
  bool waitForResolution(const std::string &requestId,
                         int timeoutSeconds = 120);

  // --- ★ 新增：外部解析权限请求（API 层回调入口） ---
  // 郑嘉娴的 POST /api/v1/permissions/{id}/approve 调用此方法
  // 钟经添的 Repository 恢复会话也可调用
  // 会通知正在 waitForResolution 中等待的线程
  // 参数: requestId - 请求 ID
  //       approved - true 同意, false 拒绝
  // 返回: 如果请求存在且状态为 Pending，则解析成功返回 true
  bool resolvePermission(const std::string &requestId, bool approved);

  // --- 处理用户响应（兼容旧接口，内部调用 resolvePermission） ---
  bool approve(const std::string &requestId);
  bool reject(const std::string &requestId);

  // --- 使过期 ---
  void expire(const std::string &requestId);

  // --- 查询请求 ---
  PermissionRequest *getRequest(const std::string &requestId);
  std::vector<PermissionRequest>
  getPendingRequests(const std::string &taskId = "") const;

  // 线程安全地按 id 拷贝一份请求（供 API 层跨线程读取）
  std::optional<PermissionRequest>
  getRequestCopy(const std::string &requestId) const;

private:
  std::shared_ptr<EventBus> eventBus_;
  std::unordered_map<std::string, PermissionRequest> requests_;
  // 保护 requests_：Agent 线程(createRequest/waitForResolution 超时 expire)与
  // API 线程(getPendingRequests/getRequestCopy/resolvePermission)会并发访问。
  mutable std::mutex requestsMutex_;

  // ★ 新增：同步等待所需的数据结构
  struct PendingWaiter {
    std::mutex mtx;
    std::condition_variable cv;
    bool resolved{false};
    bool approved{false};
  };
  std::unordered_map<std::string, std::shared_ptr<PendingWaiter>> waiters_;
  std::mutex waitersMutex_;

  size_t nextId_{1};

  std::string generateId();
  std::string currentTimestamp();
};

} // namespace codepilot
#pragma once

#include "event/EventTypes.h"
#include "facade/DataAccessFacade.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace codepilot {

class EventBus;

// ============================================================
// SSEGateway - 前后端通信统一入口（单例门面 Facade）
//
// 设计原则：
//   - 所有需要推送消息到前端的模块，统一走 SSEGateway
//   - EventBus 退居为内部运输工具，不直接面向调用方
//   - 每次 push*() 自动完成三步：
//       1. EventBus::publish() — 模块内部订阅
//       2. SSE 帧推送 — 所有订阅该 task 的客户端
//       3. DataAccessFacade::saveEvent() — 持久化到 SQLite
//
// 协议兼容性（v2.0）：
//   - event: 标签完全不变（保持 11 种事件类型）
//   - data JSON 六字段结构不变
//   - ★ 新增 metadata.channel 字段："dialog"|"status"|"debug"
//   - ★ 新增 event: stream_end（连接关闭通知）
//
// 使用示例：
//   SSEGateway::getInstance().pushDialog(taskId, "已生成 3 个步骤");
//   SSEGateway::getInstance().pushDebug(taskId, "cmake error...",
//                                        R"({"tool":"shell.run"})");
// ============================================================
class SSEGateway {
public:
  static SSEGateway &getInstance();

  // ============================================================
  // 初始化
  // ============================================================
  void init(EventBus *eventBus, DataAccessFacade *dataFacade);
  bool isInitialized() const { return initialized_; }

  // ============================================================
  // SSE 长连接入口（HttpServer 唯一调用）
  //   自动完成：发送 SSE 响应头 → 回放历史事件 → 实时推送
  //   → 心跳保活 → 终态检测 → 关闭连接
  // ============================================================
  void streamTaskEvents(int clientFd, const std::string &taskId);

  // ============================================================
  // 统一通信入口 —— 任何模块向前端发消息都走这三个方法
  // ============================================================

  // 推送对话消息（用户可见的 AI 回复内容）
  //   内部映射 → agent_message 事件
  void pushDialog(const std::string &taskId, const std::string &content,
                  const std::string &metadata = "{}");

  // 推送状态变化（任务创建、规划、完成、失败等）
  //   内部映射 → task_planning / task_completed / task_failed 等
  void pushStatus(const std::string &taskId, const std::string &content,
                  EventType eventType, const std::string &metadata = "{}");

  // 推送调试信息（工具输出、Shell 输出、错误日志等）
  //   内部映射 → tool_output / tool_started / tool_finished
  void pushDebug(const std::string &taskId, const std::string &content,
                 EventType eventType, const std::string &metadata = "{}");

  // ---- 便捷别名 ----
  // pushDialog + pushStatus 的简化版（eventType 自动推导）
  void pushMessage(const std::string &taskId, const std::string &content,
                   const std::string &metadata = "{}");
  // pushDebug 带 tool_started 的简化版
  void pushToolStarted(const std::string &taskId, const std::string &toolName,
                       const std::string &metadata = "{}");
  // pushDebug 带 tool_output 的简化版
  void pushToolOutput(const std::string &taskId, const std::string &content,
                      const std::string &metadata = "{}");
  // pushDebug 带 tool_finished 的简化版
  void pushToolFinished(const std::string &taskId, const std::string &content,
                        const std::string &metadata = "{}");

  // ============================================================
  // 查询
  // ============================================================
  int getConnectionCount(const std::string &taskId) const;

  // ============================================================
  // 子系统访问
  // ============================================================
  EventBus &eventBus(); // 给需要监听事件的模块（如 PermissionManager）

private:
  SSEGateway() = default;
  ~SSEGateway() = default;
  SSEGateway(const SSEGateway &) = delete;
  SSEGateway &operator=(const SSEGateway &) = delete;

  // 内部实现：推送一个事件到 EventBus + SSE 客户端 + 持久化
  void pushEvent(const std::string &taskId, EventType eventType,
                 const std::string &content, const std::string &metadata,
                 const std::string &channel);

  // 向所有活跃的 SSE 客户端发送帧
  void broadcastFrame(const std::string &taskId, const std::string &eventName,
                      const std::string &data);

  // 生成 ISO 8601 时间戳
  static std::string iso8601Now();
  // 生成事件 ID
  static std::string generateEventId();

  // 判断是否为终态事件
  static bool isTerminal(EventType type);

  EventBus *eventBus_ = nullptr;
  DataAccessFacade *dataFacade_ = nullptr;
  bool initialized_ = false;

  // SSE 客户端管理
  mutable std::mutex clientsMutex_;
  using SendCallback = std::function<void(const std::string &frame)>;
  struct SseClient {
    int clientFd;
    std::string taskId;
    bool alive{true};
  };
  std::vector<SseClient> liveClients_;
};

} // namespace codepilot
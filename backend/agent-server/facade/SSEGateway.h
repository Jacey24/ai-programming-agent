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
//
// 协议兼容性：
//   - 所有现有 event: 标签完全不变
//   - data JSON 六字段结构不变
//   - ★ 新增 metadata.channel 字段："dialog"|"status"|"debug"
//   - ★ 新增 event: agent_message_chunk（流式输出，第 9 点优化）
//   - ★ 新增 event: progress 标准化（第 8 点优化）
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
  // SSE 长连接入口（第 2 点优化：抽象为回调模式）
  // ============================================================

  // 新接口：通过回调函数传输（解耦底层 socket）
  using SendCallback = std::function<void(const std::string &frame)>;
  void streamTaskEvents(SendCallback sendFn, const std::string &taskId);

  // 旧接口保留兼容，内部转为调用新接口
  [[deprecated("Use streamTaskEvents(SendCallback, taskId) instead")]]
  void streamTaskEvents(int clientFd, const std::string &taskId);

  // ============================================================
  // 统一通信入口
  // ============================================================

  // 推送对话消息（用户可见的 AI 回复）
  void pushDialog(const std::string &taskId, const std::string &content,
                  const std::string &metadata = "{}");

  // 流式推送对话消息（第 9 点优化）
  //   发送 event: agent_message_chunk 片段，最后自动补全 agent_message
  void pushDialogStream(const std::string &taskId, const std::string &chunk,
                        bool isLast = false,
                        const std::string &fullContent = "");

  // 推送状态变化
  void pushStatus(const std::string &taskId, const std::string &content,
                  EventType eventType, const std::string &metadata = "{}");

  // 推送调试信息
  void pushDebug(const std::string &taskId, const std::string &content,
                 EventType eventType, const std::string &metadata = "{}");

  // ---- 便捷别名 ----
  void pushMessage(const std::string &taskId, const std::string &content,
                   const std::string &metadata = "{}");
  void pushToolStarted(const std::string &taskId, const std::string &toolName,
                       const std::string &metadata = "{}");
  void pushToolOutput(const std::string &taskId, const std::string &content,
                      const std::string &metadata = "{}");
  void pushToolFinished(const std::string &taskId, const std::string &content,
                        const std::string &metadata = "{}");

  // ============================================================
  // 进度推送（第 8 点优化）
  //   标准化进度格式：{"current": 2, "total": 5, "action": "正在编译..."}
  //   发送 event: progress 事件
  // ============================================================
  void pushProgress(const std::string &taskId, int current, int total,
                    const std::string &action = "",
                    const std::string &metadata = "{}");

  // ============================================================
  // 查询
  // ============================================================
  int getConnectionCount(const std::string &taskId) const;

  // ============================================================
  // 子系统访问
  // ============================================================
  EventBus &eventBus();

private:
  SSEGateway() = default;
  ~SSEGateway() = default;
  SSEGateway(const SSEGateway &) = delete;
  SSEGateway &operator=(const SSEGateway &) = delete;

  // 内部推送事件到 EventBus + SSE 客户端 + 持久化
  void pushEvent(const std::string &taskId, EventType eventType,
                 const std::string &content, const std::string &metadata,
                 const std::string &channel);

  // 广播帧到所有活跃 SSE 客户端（基于回调模式）
  void broadcastFrame(const std::string &taskId, const std::string &eventName,
                      const std::string &data);

  static std::string iso8601Now();
  static std::string generateEventId();
  static bool isTerminal(EventType type);

  // streamTaskEvents 内部实现
  void streamTaskEventsImpl(const std::string &taskId,
                            const SendCallback &sendFn);

  EventBus *eventBus_ = nullptr;
  DataAccessFacade *dataFacade_ = nullptr;
  bool initialized_ = false;

  // SSE 客户端管理（基于回调的客户）
  mutable std::mutex clientsMutex_;
  struct SseClient {
    std::string taskId;
    bool alive{true};
    SendCallback sendFn;
  };
  std::vector<SseClient> liveClients_;
};

} // namespace codepilot
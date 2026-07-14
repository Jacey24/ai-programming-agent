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
// SSEGateway - 全局数据中枢（单例门面 Facade）
//
// 核心设计：一个入口函数 + 两组枚举 = 完全决定数据的三个去向
//   - push() 是唯一核心入口
//   - 三个去向：① EventBus 内部广播 → ② SSE 推送到前端 → ③ SQLite 持久化
//   - Channel 枚举 → 决定数据的频道归属（dialog/status/debug）
//   - Persist 枚举 → 决定是否落库（Always/Never）
//   - 所有便捷别名（pushDialog/pushStatus/pushDebug 等）内部调 push()
//
// 历史：原为 SSE 推送专用网关，随架构演进成为统一的数据路由/持久化入口。
//       命名保留以保持团队认知连续性，职责范围已远超「SSE 网关」。
//
// 协议兼容性：
//   - 所有现有 event: 标签完全不变
//   - data JSON 六字段结构不变
//   - ★ metadata.channel 字段: "dialog"|"status"|"debug"
//   - ★ event: agent_message_chunk（流式输出）
//   - ★ event: progress（标准化进度）
// ============================================================
class SSEGateway {
public:
  static SSEGateway &getInstance();

  // ============================================================
  // 数据去向枚举
  // ============================================================

  // 频道 —— 决定 SSE metadata.channel + EventBus 行为
  enum class Channel {
    Dialog, // 对话消息 → metadata.channel: "dialog"
    Status, // 状态变化 → metadata.channel: "status"
    Debug,  // 调试信息 → metadata.channel: "debug"
  };

  // 持久化策略 —— 决定是否写入 SQLite
  enum class Persist {
    Always, // 落库（默认）
    Never,  // 不落库（心跳、流式片段、瞬态进度）
  };

  // ============================================================
  // 初始化
  // ============================================================
  void init(EventBus *eventBus, DataAccessFacade *dataFacade);
  bool isInitialized() const { return initialized_; }

  // ============================================================
  // ★ 统一入口：一组参数完全决定数据的去向
  //
  //   参数：
  //     taskId    - 任务 ID
  //     eventType - 事件类型（11 种 EventType 枚举）
  //     content   - 内容字符串
  //     metadata  - 额外的 JSON 元数据（可选）
  //     channel   - 频道（Dialog/Status/Debug）
  //     persist   - 持久化策略（Always/Never）
  //
  //   自动完成：
  //     ① EventBus::publish() — 模块内部广播
  //     ② broadcastFrame()   — SSE 推送到前端
  //     ③ saveEvent()        — 写入 SQLite（仅 persist=Always）
  // ============================================================
  void push(const std::string &taskId, EventType eventType,
            const std::string &content, const json &metadata = json::object(),
            Channel channel = Channel::Status,
            Persist persist = Persist::Always);

  // ============================================================
  // 流式推送（第 9 点优化）
  //   自动处理 chunk 不持久化 + isLast 时落库完整内容
  // ============================================================
  void pushStream(const std::string &taskId, const std::string &chunk,
                  bool isLast = false, const std::string &fullContent = "");

  // ============================================================
  // 便捷别名（内部全部调 push()，保持向后兼容）
  // ============================================================

  // 对话消息
  void pushDialog(const std::string &taskId, const std::string &content,
                  const std::string &metadata = "{}");
  // 状态变化
  void pushStatus(const std::string &taskId, const std::string &content,
                  EventType eventType, const std::string &metadata = "{}");
  // 调试信息
  void pushDebug(const std::string &taskId, const std::string &content,
                 EventType eventType, const std::string &metadata = "{}");
  // 推送对话消息（等价于 pushDialog）
  void pushMessage(const std::string &taskId, const std::string &content,
                   const std::string &metadata = "{}");
  // 工具开始
  void pushToolStarted(const std::string &taskId, const std::string &toolName,
                       const std::string &metadata = "{}");
  // 工具输出
  void pushToolOutput(const std::string &taskId, const std::string &content,
                      const std::string &metadata = "{}");
  // 工具完成
  void pushToolFinished(const std::string &taskId, const std::string &content,
                        const std::string &metadata = "{}");
  // 进度推送
  void pushProgress(const std::string &taskId, int current, int total,
                    const std::string &action = "",
                    const std::string &metadata = "{}");

  // ============================================================
  // SSE 长连接入口
  // ============================================================
  using SendCallback = std::function<void(const std::string &frame)>;
  void streamTaskEvents(SendCallback sendFn, const std::string &taskId);
  [[deprecated("Use streamTaskEvents(SendCallback, taskId) instead")]]
  void streamTaskEvents(int clientFd, const std::string &taskId);

  // ============================================================
  // 查询与子系统访问
  // ============================================================
  int getConnectionCount(const std::string &taskId) const;
  EventBus &eventBus();

private:
  SSEGateway() = default;
  ~SSEGateway() = default;
  SSEGateway(const SSEGateway &) = delete;
  SSEGateway &operator=(const SSEGateway &) = delete;

  // 将 Channel 枚举转为字符串
  static std::string channelToString(Channel ch);

  // 广播帧到所有活跃 SSE 客户端
  void broadcastFrame(const std::string &taskId, const std::string &eventName,
                      const std::string &data);

  static std::string iso8601Now();
  static std::string generateEventId();
  static bool isTerminal(EventType type);
  void streamTaskEventsImpl(const std::string &taskId,
                            const SendCallback &sendFn);

  EventBus *eventBus_ = nullptr;
  DataAccessFacade *dataFacade_ = nullptr;
  bool initialized_ = false;

  // SSE 客户端管理
  mutable std::mutex clientsMutex_;
  struct SseClient {
    std::string taskId;
    bool alive{true};
    bool directPush{
        true}; // false = only EventBus callback, skip broadcastFrame
    SendCallback sendFn;
  };
  std::vector<SseClient> liveClients_;
};

} // namespace codepilot
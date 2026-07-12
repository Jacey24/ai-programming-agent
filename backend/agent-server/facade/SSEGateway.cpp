#include "facade/SSEGateway.h"
#include "event/EventBus.h"
#include "facade/DataAccessFacade.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace codepilot {

// ============================================================
// 单例获取
// ============================================================
SSEGateway &SSEGateway::getInstance() {
  static SSEGateway instance;
  return instance;
}

// ============================================================
// 工具函数
// ============================================================
std::string SSEGateway::iso8601Now() {
  auto t =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm *tm = std::gmtime(&t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return buf;
}

std::string SSEGateway::generateEventId() {
  static std::atomic<uint64_t> counter{0};
  const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
  return "event_" + std::to_string(ts) + "_" + std::to_string(++counter);
}

bool SSEGateway::isTerminal(EventType type) {
  return type == EventType::TaskCompleted || type == EventType::TaskFailed ||
         type == EventType::TaskCancelled;
}

// ============================================================
// 初始化
// ============================================================
void SSEGateway::init(EventBus *eventBus, DataAccessFacade *dataFacade) {
  if (initialized_ || !eventBus || !dataFacade) {
    return;
  }
  eventBus_ = eventBus;
  dataFacade_ = dataFacade;
  initialized_ = true;
}

// ============================================================
// 子系统访问
// ============================================================
EventBus &SSEGateway::eventBus() {
  if (!eventBus_) {
    throw std::runtime_error("SSEGateway not initialized");
  }
  return *eventBus_;
}

// ============================================================
// 查询连接数
// ============================================================
int SSEGateway::getConnectionCount(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(clientsMutex_);
  int count = 0;
  for (const auto &c : liveClients_) {
    if (c.taskId == taskId && c.alive) {
      ++count;
    }
  }
  return count;
}

// ============================================================
// 广播帧到所有匹配 taskId 的活跃 SSE 客户端
// ============================================================
void SSEGateway::broadcastFrame(const std::string &taskId,
                                const std::string &eventName,
                                const std::string &data) {
  std::string frame = "event: " + eventName + "\ndata: " + data + "\n\n";

  std::lock_guard<std::mutex> lock(clientsMutex_);
  for (auto &client : liveClients_) {
    if (client.taskId == taskId && client.alive) {
      try {
        client.sendFn(frame);
      } catch (...) {
        client.alive = false;
      }
    }
  }
  liveClients_.erase(
      std::remove_if(liveClients_.begin(), liveClients_.end(),
                     [](const SseClient &c) { return !c.alive; }),
      liveClients_.end());
}

// ============================================================
// 核心：推送事件（EventBus + SSE 广播 + 持久化）
// ============================================================
void SSEGateway::pushEvent(const std::string &taskId, EventType eventType,
                           const std::string &content,
                           const std::string &metadata,
                           const std::string &channel) {
  if (!initialized_) {
    return;
  }

  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  meta["channel"] = channel;

  const std::string eventId = generateEventId();
  const std::string now = iso8601Now();

  if (eventBus_) {
    EventData event = EventData::Create(taskId, eventType, content, meta);
    event.id = eventId;
    event.createdAt = now;
    eventBus_->publish(event);
  }

  json sseData;
  sseData["id"] = eventId;
  sseData["task_id"] = taskId;
  sseData["type"] = EventData::Create("", eventType, "", {}).typeToString();
  sseData["content"] = content;
  sseData["metadata"] = meta;
  sseData["created_at"] = now;

  broadcastFrame(taskId, sseData["type"].get<std::string>(), sseData.dump());

  if (dataFacade_ && dataFacade_->isInitialized()) {
    dataFacade_->saveEvent(eventId, taskId, sseData["type"].get<std::string>(),
                           content, meta.dump());
  }
}

// ============================================================
// pushDialog — 对话消息
// ============================================================
void SSEGateway::pushDialog(const std::string &taskId,
                            const std::string &content,
                            const std::string &metadata) {
  pushEvent(taskId, EventType::AgentMessage, content, metadata, "dialog");
}

// ============================================================
// pushDialogStream — 流式对话（第 9 点优化）
// ============================================================
void SSEGateway::pushDialogStream(const std::string &taskId,
                                  const std::string &chunk, bool isLast,
                                  const std::string &fullContent) {
  if (!initialized_) {
    return;
  }

  const std::string eventId = generateEventId();
  const std::string now = iso8601Now();

  // 发送 agent_message_chunk 片段
  json chunkData;
  chunkData["id"] = eventId;
  chunkData["task_id"] = taskId;
  chunkData["type"] = "agent_message_chunk";
  chunkData["content"] = chunk;
  chunkData["metadata"] = json{{"channel", "dialog"}, {"streaming", true}};
  chunkData["created_at"] = now;

  broadcastFrame(taskId, "agent_message_chunk", chunkData.dump());

  if (isLast) {
    // 发送完整的 agent_message（用于前端替换增量内容）
    json fullData;
    fullData["id"] = eventId;
    fullData["task_id"] = taskId;
    fullData["type"] = "agent_message";
    fullData["content"] = fullContent.empty() ? chunk : fullContent;
    fullData["metadata"] =
        json{{"channel", "dialog"}, {"streaming", false}, {"stream_end", true}};
    fullData["created_at"] = now;

    broadcastFrame(taskId, "agent_message", fullData.dump());

    // 持久化完整内容
    if (dataFacade_ && dataFacade_->isInitialized()) {
      dataFacade_->saveEvent(
          eventId, taskId, "agent_message",
          fullContent.empty() ? chunk : fullContent,
          json{{"channel", "dialog"}, {"stream_end", true}}.dump());
    }
  }
}

// ============================================================
// pushStatus — 状态变化
// ============================================================
void SSEGateway::pushStatus(const std::string &taskId,
                            const std::string &content, EventType eventType,
                            const std::string &metadata) {
  pushEvent(taskId, eventType, content, metadata, "status");
}

// ============================================================
// pushDebug — 调试信息
// ============================================================
void SSEGateway::pushDebug(const std::string &taskId,
                           const std::string &content, EventType eventType,
                           const std::string &metadata) {
  pushEvent(taskId, eventType, content, metadata, "debug");
}

// ============================================================
// pushProgress — 标准化进度推送（第 8 点优化）
// ============================================================
void SSEGateway::pushProgress(const std::string &taskId, int current, int total,
                              const std::string &action,
                              const std::string &metadata) {
  if (!initialized_) {
    return;
  }

  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  meta["channel"] = "status";
  meta["progress"] = {{"current", current}, {"total", total}};
  if (!action.empty()) {
    meta["progress"]["action"] = action;
  }

  const std::string eventId = generateEventId();
  const std::string now = iso8601Now();

  json sseData;
  sseData["id"] = eventId;
  sseData["task_id"] = taskId;
  sseData["type"] = "progress";
  sseData["content"] =
      action.empty() ? std::to_string(current) + "/" + std::to_string(total)
                     : action;
  sseData["metadata"] = meta;
  sseData["created_at"] = now;

  broadcastFrame(taskId, "progress", sseData.dump());
}

// ============================================================
// 便捷别名
// ============================================================
void SSEGateway::pushMessage(const std::string &taskId,
                             const std::string &content,
                             const std::string &metadata) {
  pushDialog(taskId, content, metadata);
}

void SSEGateway::pushToolStarted(const std::string &taskId,
                                 const std::string &toolName,
                                 const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  meta["tool_name"] = toolName;
  pushDebug(taskId, "开始执行 " + toolName, EventType::ToolStarted,
            meta.dump());
}

void SSEGateway::pushToolOutput(const std::string &taskId,
                                const std::string &content,
                                const std::string &metadata) {
  pushDebug(taskId, content, EventType::ToolOutput, metadata);
}

void SSEGateway::pushToolFinished(const std::string &taskId,
                                  const std::string &content,
                                  const std::string &metadata) {
  pushDebug(taskId, content, EventType::ToolFinished, metadata);
}

// ============================================================
// 旧接口：通过 clientFd（第 2 点优化包装）
// ============================================================
void SSEGateway::streamTaskEvents(int clientFd, const std::string &taskId) {
  // 将 clientFd 包装为回调，转发到新实现
  auto sendFn = [clientFd](const std::string &frame) {
    send(clientFd, frame.data(), frame.size(), MSG_NOSIGNAL);
  };
  streamTaskEvents(sendFn, taskId);
}

// ============================================================
// 新接口：通过回调（第 2 点优化）
// ============================================================
void SSEGateway::streamTaskEvents(SendCallback sendFn,
                                  const std::string &taskId) {
  if (!initialized_) {
    const std::string errFrame =
        "event: error\ndata: "
        "{\"type\":\"error\",\"content\":\"SSEGateway not initialized\"}\n\n";
    sendFn(errFrame);
    return;
  }

  // 1) 发送 SSE 响应头（仅保留 HTTP 协议兼容性）
  const std::string headers =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream; charset=utf-8\r\n"
      "Cache-Control: no-cache\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Connection: keep-alive\r\n\r\n";
  sendFn(headers);

  // 2) 委托给内部实现
  streamTaskEventsImpl(taskId, sendFn);
}

// ============================================================
// streamTaskEvents 内部实现（第 2 点优化：共享逻辑）
// ============================================================
void SSEGateway::streamTaskEventsImpl(const std::string &taskId,
                                      const SendCallback &sendFn) {
  if (!initialized_) {
    return;
  }

  // 注册客户端
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    liveClients_.push_back({taskId, true, sendFn});
  }

  // 订阅 EventBus 实时事件
  std::mutex writeMutex;
  std::atomic_bool alive{true};
  std::atomic_bool done{false};

  auto writeFrame = [&](const std::string &eventName, const std::string &data) {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (!alive.load()) {
      return;
    }
    std::string frame = "event: " + eventName + "\ndata: " + data + "\n\n";
    try {
      sendFn(frame);
    } catch (...) {
      alive.store(false);
    }
  };

  ListenerId listenerId = 0;
  bool subscribed = false;
  if (eventBus_) {
    listenerId = eventBus_->subscribeByTaskId(taskId, [&](const EventData &ev) {
      json data;
      data["id"] = ev.id;
      data["task_id"] = ev.taskId;
      data["type"] = ev.typeToString();
      data["content"] = ev.content;
      data["metadata"] = ev.metadata;
      data["created_at"] = ev.createdAt;
      writeFrame(ev.typeToString(), data.dump());
      if (isTerminal(ev.type)) {
        done.store(true);
      }
    });
    subscribed = true;
  }

  // 回放历史事件
  if (dataFacade_ && dataFacade_->isInitialized()) {
    for (const auto &ev : dataFacade_->getEventsByTaskId(taskId)) {
      json data;
      data["id"] = ev.id;
      data["task_id"] = ev.task_id;
      data["type"] = ev.type;
      data["content"] = ev.content;

      json meta = json::parse(ev.metadata, nullptr, false);
      if (meta.is_discarded()) {
        meta = json::object();
      }
      data["metadata"] = meta;
      data["created_at"] = ev.created_at;

      writeFrame(ev.type, data.dump());
      if (ev.type == "task_completed" || ev.type == "task_failed" ||
          ev.type == "task_cancelled") {
        done.store(true);
      }
    }
  }

  // 心跳保活 + 结束检测
  int ticks = 0;
  while (alive.load() && !done.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    {
      std::lock_guard<std::mutex> lock(writeMutex);
      const std::string ping = ": ping\n\n";
      try {
        sendFn(ping);
      } catch (...) {
        alive.store(false);
        break;
      }
    }
    if (++ticks % 3 == 0 && dataFacade_ && dataFacade_->isInitialized()) {
      auto task = dataFacade_->getTask(taskId);
      if (task && (task->status == "completed" || task->status == "failed" ||
                   task->status == "cancelled")) {
        done.store(true);
      }
    }
  }

  // 发送 stream_end 并清理
  {
    std::lock_guard<std::mutex> lock(writeMutex);
    const std::string endFrame =
        "event: stream_end\ndata: {\"type\":\"stream_end\"}\n\n";
    try {
      sendFn(endFrame);
    } catch (...) {
    }
  }

  if (subscribed && eventBus_) {
    eventBus_->unsubscribe(listenerId);
  }

  // 注销客户端（通过 sendFn 指针匹配）
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto target = &sendFn;
    (void)target;
    liveClients_.erase(std::remove_if(liveClients_.begin(), liveClients_.end(),
                                      [&sendFn](const SseClient &c) {
                                        return c.sendFn.target_type() ==
                                               sendFn.target_type();
                                      }),
                       liveClients_.end());
  }
}

} // namespace codepilot
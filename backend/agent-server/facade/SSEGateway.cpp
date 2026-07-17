#include "facade/SSEGateway.h"
#include "event/EventBus.h"
#include "facade/DataAccessFacade.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <unistd.h>
#define close_socket close
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

namespace {

std::string dumpJsonForTransport(const json &value) {
  return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

} // namespace

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

bool isTerminalTaskStatus(const std::string &status) {
  return status == "completed" || status == "failed" ||
         status == "cancelled" || status == "interrupted";
}

std::string SSEGateway::channelToString(Channel ch) {
  switch (ch) {
  case Channel::Dialog:
    return "dialog";
  case Channel::Status:
    return "status";
  case Channel::Debug:
    return "debug";
  }
  return "status"; // fallback
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
  for (const auto &[id, connection] : liveClients_) {
    (void)id;
    if (connection->taskId == taskId && connection->signal->connected.load()) {
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

  std::vector<std::shared_ptr<ClientConnection>> clients;
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto &[id, connection] : liveClients_) {
      (void)id;
      if (connection->taskId == taskId && connection->directPush &&
          connection->signal->connected.load()) {
        clients.push_back(connection);
      }
    }
  }

  for (const auto &connection : clients) {
    std::lock_guard<std::mutex> writeLock(connection->writeMutex);
    if (!connection->signal->connected.load()) {
      continue;
    }
    try {
      if (!connection->sendFn || !connection->sendFn(frame)) {
        connection->signal->close();
      }
    } catch (...) {
      connection->signal->close();
    }
  }
}

// ============================================================
// ★ 统一入口：push() — 一组参数完全决定数据去向
//
// 自动完成三步操作：
//   ① EventBus::publish() — 模块内部广播
//   ② broadcastFrame()   — SSE 推送到前端
//   ③ saveEvent()        — 写入 SQLite（仅 persist=Always）
// ============================================================
void SSEGateway::push(const std::string &taskId, EventType eventType,
                      const std::string &content, const json &metadata,
                      Channel channel, Persist persist) {
  if (!initialized_) {
    return;
  }

  // 构造 metadata（注入 channel）
  json meta = metadata.is_object() ? metadata : json::object();
  meta["channel"] = channelToString(channel);

  const std::string eventId = generateEventId();
  const std::string now = iso8601Now();
  const std::string eventTypeStr =
      EventData::Create("", eventType, "", {}).typeToString();

  // ① EventBus 广播
  if (eventBus_) {
    EventData event = EventData::Create(taskId, eventType, content, meta);
    event.id = eventId;
    event.createdAt = now;
    eventBus_->publish(event);
  }

  // ② SSE 推送到前端
  json sseData;
  sseData["id"] = eventId;
  sseData["task_id"] = taskId;
  sseData["type"] = eventTypeStr;
  sseData["content"] = content;
  sseData["metadata"] = meta;
  sseData["created_at"] = now;
  broadcastFrame(taskId, eventTypeStr, dumpJsonForTransport(sseData));

  // ③ 持久化到 SQLite（仅 persist=Always）
  if (persist == Persist::Always && dataFacade_ &&
      dataFacade_->isInitialized()) {
    dataFacade_->saveEvent(eventId, taskId, eventTypeStr, content,
                           dumpJsonForTransport(meta));
  }
}

// ============================================================
// pushStream — 流式推送
// ============================================================
void SSEGateway::pushStream(const std::string &taskId,
                            const std::string &messageId,
                            const std::string &chunk, std::size_t sequence,
                            bool isLast, const std::string &fullContent) {
  if (!initialized_) {
    return;
  }

  const std::string now = iso8601Now();

  // 当前浏览器 SSE 连接订阅 EventBus，且 directPush=false。流式片段只走
  // EventBus，避免与 broadcastFrame 形成双发送路径。
  if (!chunk.empty() && eventBus_) {
    EventData event = EventData::Create(
        taskId, EventType::AgentMessageChunk, chunk,
        json{{"channel", "dialog"},
             {"message_id", messageId},
             {"sequence", sequence},
             {"streaming", true},
             {"done", false}});
    event.id = generateEventId();
    event.createdAt = now;
    eventBus_->publish(event);
  }

  if (isLast) {
    const std::string eventId = generateEventId();
    const std::string finalContent = fullContent.empty() ? chunk : fullContent;
    const json finalMeta{{"channel", "dialog"},
                        {"message_id", messageId},
                        {"sequence", sequence},
                        {"streaming", false},
                        {"done", true},
                        {"stream_end", true}};

    if (eventBus_) {
      EventData event =
          EventData::Create(taskId, EventType::AgentMessage, finalContent,
                            finalMeta);
      event.id = eventId;
      event.createdAt = now;
      eventBus_->publish(event);
    }

    // 持久化完整内容
    if (dataFacade_ && dataFacade_->isInitialized()) {
      dataFacade_->saveEvent(eventId, taskId, "agent_message", finalContent,
                             dumpJsonForTransport(finalMeta));
    }
  }
}

// ============================================================
// 便捷别名 — 全部内部调 push()
// ============================================================

void SSEGateway::pushDialog(const std::string &taskId,
                            const std::string &content,
                            const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  push(taskId, EventType::AgentMessage, content, meta, Channel::Dialog,
       Persist::Always);
}

void SSEGateway::pushStatus(const std::string &taskId,
                            const std::string &content, EventType eventType,
                            const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  push(taskId, eventType, content, meta, Channel::Status, Persist::Always);
}

void SSEGateway::pushDebug(const std::string &taskId,
                           const std::string &content, EventType eventType,
                           const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  push(taskId, eventType, content, meta, Channel::Debug, Persist::Always);
}

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
  push(taskId, EventType::ToolStarted, "开始执行 " + toolName, meta,
       Channel::Debug, Persist::Always);
}

void SSEGateway::pushToolOutput(const std::string &taskId,
                                const std::string &content,
                                const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  push(taskId, EventType::ToolOutput, content, meta, Channel::Debug,
       Persist::Always);
}

void SSEGateway::pushToolFinished(const std::string &taskId,
                                  const std::string &content,
                                  const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  push(taskId, EventType::ToolFinished, content, meta, Channel::Debug,
       Persist::Always);
}

void SSEGateway::pushProgress(const std::string &taskId, int current, int total,
                              const std::string &action,
                              const std::string &metadata) {
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  meta["progress"] = {{"current", current}, {"total", total}};
  if (!action.empty()) {
    meta["progress"]["action"] = action;
  }

  // progress 是瞬态，Persist::Never
  push(taskId, EventType::AgentMessage,
       action.empty() ? std::to_string(current) + "/" + std::to_string(total)
                      : action,
       meta, Channel::Status, Persist::Never);
}

// ============================================================
// 旧接口：通过 clientFd（已弃用，保留向后兼容）
// ============================================================
void SSEGateway::streamTaskEvents(int clientFd, const std::string &taskId) {
  // 先发 HTTP 响应头（旧接口自己处理，因为不经过 httplib）
  const std::string headers =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream; charset=utf-8\r\n"
      "Cache-Control: no-cache\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Connection: keep-alive\r\n\r\n";
  const int headerBytes =
      send(clientFd, headers.data(), static_cast<int>(headers.size()),
           MSG_NOSIGNAL);
  if (headerBytes != static_cast<int>(headers.size())) {
    close_socket(clientFd);
    return;
  }

  auto sendFn = [clientFd](const std::string &frame) {
    const int sent = send(clientFd, frame.data(),
                          static_cast<int>(frame.size()), MSG_NOSIGNAL);
    return sent == static_cast<int>(frame.size());
  };
  streamTaskEvents(sendFn, taskId);
  close_socket(clientFd);
}

// ============================================================
// 新接口：通过回调（不发送 HTTP 头 —— 由 httplib 等上层处理）
// ============================================================
void SSEGateway::streamTaskEvents(
    SendCallback sendFn, const std::string &taskId,
    std::shared_ptr<ConnectionSignal> connectionSignal) {
  if (!initialized_) {
    const std::string errFrame =
        "event: error\ndata: "
        "{\"type\":\"error\",\"content\":\"SSEGateway not initialized\"}\n\n";
    sendFn(errFrame);
    if (connectionSignal) {
      connectionSignal->close();
    }
    return;
  }

  streamTaskEventsImpl(taskId, std::move(sendFn),
                       std::move(connectionSignal));
}

// ============================================================
// streamTaskEvents 内部实现
// ============================================================
void SSEGateway::streamTaskEventsImpl(
    const std::string &taskId, SendCallback sendFn,
    std::shared_ptr<ConnectionSignal> connectionSignal) {
  if (!initialized_) {
    return;
  }

  if (!connectionSignal) {
    connectionSignal = std::make_shared<ConnectionSignal>();
  }
  const auto connectionId =
      nextConnectionId_.fetch_add(1, std::memory_order_relaxed);
  auto connection = std::make_shared<ClientConnection>();
  connection->connectionId = connectionId;
  connection->taskId = taskId;
  connection->directPush = false;
  connection->sendFn = std::move(sendFn);
  connection->signal = std::move(connectionSignal);

  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    liveClients_.emplace(connectionId, connection);
  }

  ListenerId listenerId = 0;
  bool subscribed = false;
  auto cleanup = [&] {
    if (connection->cleanupStarted.exchange(true)) {
      return;
    }
    connection->signal->close();
    if (subscribed && eventBus_) {
      eventBus_->unsubscribe(listenerId);
      subscribed = false;
    }
    {
      std::lock_guard<std::mutex> lock(clientsMutex_);
      auto it = liveClients_.find(connectionId);
      if (it != liveClients_.end() && it->second == connection) {
        liveClients_.erase(it);
      }
    }
    std::lock_guard<std::mutex> writeLock(connection->writeMutex);
    connection->sendFn = nullptr;
  };

  auto sendFrame = [connection](const std::string &frame) {
    std::lock_guard<std::mutex> lock(connection->writeMutex);
    if (!connection->signal->connected.load()) {
      return false;
    }
    try {
      if (!connection->sendFn || !connection->sendFn(frame)) {
        connection->signal->close();
        return false;
      }
      return true;
    } catch (...) {
      connection->signal->close();
      return false;
    }
  };
  auto writeFrame = [sendFrame](const std::string &eventName,
                                const std::string &data) {
    return sendFrame("event: " + eventName + "\ndata: " + data + "\n\n");
  };

  try {
    if (eventBus_) {
      listenerId = eventBus_->subscribeByTaskId(
          taskId, [connection, writeFrame](const EventData &ev) {
            json data;
            data["id"] = ev.id;
            data["task_id"] = ev.taskId;
            data["type"] = ev.typeToString();
            data["content"] = ev.content;
            data["metadata"] = ev.metadata;
            data["created_at"] = ev.createdAt;
            writeFrame(ev.typeToString(), dumpJsonForTransport(data));
            if (isTerminal(ev.type)) {
              connection->done.store(true);
              connection->signal->cv.notify_all();
            }
          });
      subscribed = true;
    }

    // 回放历史事件
    if (dataFacade_ && dataFacade_->isInitialized()) {
      for (const auto &ev : dataFacade_->getEventsByTaskId(taskId)) {
        if (!connection->signal->connected.load()) {
          break;
        }
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

        writeFrame(ev.type, dumpJsonForTransport(data));
        if (ev.type == "task_completed" || ev.type == "task_failed" ||
            ev.type == "task_cancelled") {
          connection->done.store(true);
          connection->signal->cv.notify_all();
        }
      }

      // A recovered task can be terminal even when no new terminal event was
      // required. End the stream from the persisted status without emitting
      // or persisting another event.
      if (!connection->done.load()) {
        const auto task = dataFacade_->getTask(taskId);
        if (task && task->status == "interrupted") {
          connection->done.store(true);
          connection->signal->cv.notify_all();
        }
      }
    }

    // 心跳保活 + 结束检测
    int ticks = 0;
    while (connection->signal->connected.load() &&
           !connection->done.load()) {
      std::unique_lock<std::mutex> stateLock(connection->signal->mutex);
      connection->signal->cv.wait_for(
          stateLock, std::chrono::seconds(3), [&] {
            return !connection->signal->connected.load() ||
                   connection->done.load();
          });
      stateLock.unlock();
      if (!connection->signal->connected.load() || connection->done.load()) {
        break;
      }
      sendFrame(": ping\n\n");
      if (!connection->signal->connected.load()) {
        break;
      }
      if (++ticks % 3 == 0 && dataFacade_ && dataFacade_->isInitialized()) {
        auto task = dataFacade_->getTask(taskId);
        if (task && isTerminalTaskStatus(task->status)) {
          connection->done.store(true);
          connection->signal->cv.notify_all();
        }
      }
    }

    if (connection->signal->connected.load() && connection->done.load()) {
      writeFrame("stream_end", "{\"type\":\"stream_end\"}");
    }
  } catch (...) {
    cleanup();
    return;
  }

  cleanup();
}

} // namespace codepilot

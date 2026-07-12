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
      if (send(client.clientFd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0) {
        client.alive = false;
      }
    }
  }
  // 清理已断开的连接
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

  // 1. 构造 metadata（注入 channel）
  json meta = json::parse(metadata, nullptr, false);
  if (meta.is_discarded()) {
    meta = json::object();
  }
  meta["channel"] = channel;

  const std::string eventId = generateEventId();
  const std::string now = iso8601Now();

  // 2. 发布到 EventBus（模块内部订阅用）
  if (eventBus_) {
    EventData event = EventData::Create(taskId, eventType, content, meta);
    // 覆盖自动生成的 id 和 createdAt，保持一致
    event.id = eventId;
    event.createdAt = now;
    eventBus_->publish(event);
  }

  // 3. 广播到 SSE 客户端
  json sseData;
  sseData["id"] = eventId;
  sseData["task_id"] = taskId;
  sseData["type"] = EventData::Create("", eventType, "", {})
                        .typeToString(); // 使用已有的类型转换
  sseData["content"] = content;
  sseData["metadata"] = meta;
  sseData["created_at"] = now;

  broadcastFrame(taskId, sseData["type"].get<std::string>(), sseData.dump());

  // 4. 持久化到 SQLite
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
// SSE 长连接入口 —— HttpServer 唯一调用
// ============================================================
void SSEGateway::streamTaskEvents(int clientFd, const std::string &taskId) {
  if (!initialized_) {
    const std::string errFrame =
        "event: error\ndata: "
        "{\"type\":\"error\",\"content\":\"SSEGateway not initialized\"}\n\n";
    send(clientFd, errFrame.data(), errFrame.size(), MSG_NOSIGNAL);
    close(clientFd);
    return;
  }

  // 1) 发送 SSE 响应头
  const std::string headers =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream; charset=utf-8\r\n"
      "Cache-Control: no-cache\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Connection: keep-alive\r\n\r\n";
  if (send(clientFd, headers.data(), headers.size(), MSG_NOSIGNAL) < 0) {
    close(clientFd);
    return;
  }

  // 2) 注册客户端
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    liveClients_.push_back({clientFd, taskId, true});
  }

  // 3) 订阅 EventBus 实时事件
  std::mutex writeMutex;
  std::atomic_bool alive{true};
  std::atomic_bool done{false};

  auto writeFrame = [&](const std::string &eventName, const std::string &data) {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (!alive.load()) {
      return;
    }
    std::string frame = "event: " + eventName + "\ndata: " + data + "\n\n";
    if (send(clientFd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0) {
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

  // 4) 回放历史事件（从 DataAccessFacade）
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
      // 用 type 字符串判断终态
      if (ev.type == "task_completed" || ev.type == "task_failed" ||
          ev.type == "task_cancelled") {
        done.store(true);
      }
    }
  }

  // 5) 心跳保活 + 结束检测
  int ticks = 0;
  while (alive.load() && !done.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    {
      std::lock_guard<std::mutex> lock(writeMutex);
      const std::string ping = ": ping\n\n";
      if (send(clientFd, ping.data(), ping.size(), MSG_NOSIGNAL) < 0) {
        alive.store(false);
        break;
      }
    }
    // 每 3 个 tick（9 秒）兜底检查任务状态
    if (++ticks % 3 == 0 && dataFacade_ && dataFacade_->isInitialized()) {
      auto task = dataFacade_->getTask(taskId);
      if (task && (task->status == "completed" || task->status == "failed" ||
                   task->status == "cancelled")) {
        done.store(true);
      }
    }
  }

  // 6) 发送 stream_end 并清理
  {
    std::lock_guard<std::mutex> lock(writeMutex);
    const std::string endFrame =
        "event: stream_end\ndata: {\"type\":\"stream_end\"}\n\n";
    send(clientFd, endFrame.data(), endFrame.size(), MSG_NOSIGNAL);
  }

  if (subscribed && eventBus_) {
    eventBus_->unsubscribe(listenerId);
  }

  // 注销客户端
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    liveClients_.erase(std::remove_if(liveClients_.begin(), liveClients_.end(),
                                      [clientFd](const SseClient &c) {
                                        return c.clientFd == clientFd;
                                      }),
                       liveClients_.end());
  }

  close(clientFd);
}

} // namespace codepilot
#include "EventBus.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace codepilot {

// ============================================================
// SSE 连接客户端
// ============================================================
struct SseClient {
  std::string clientId;
  std::string taskId;
  std::string lastEventId;
  std::function<void(const std::string &)> sendCallback; // 发送 SSE 数据的回调
  bool connected{true};
};

// ============================================================
// EventDispatcher - SSE 事件分发器
// 负责将 EventBus 上的事件分发给对应的 SSE 连接
// ============================================================
class EventDispatcher {
public:
  EventDispatcher(EventBus &eventBus) : eventBus_(eventBus), listenerId_(0) {
    // 订阅所有事件
    listenerId_ = eventBus_.subscribeAll(
        [this](const EventData &event) { this->onEvent(event); });
  }

  ~EventDispatcher() { eventBus_.unsubscribe(listenerId_); }

  // --- 注册 SSE 客户端（连接到指定 task） ---
  std::string
  registerClient(const std::string &taskId,
                 std::function<void(const std::string &)> sendCallback) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string clientId = "sse_" + std::to_string(nextClientId_++);
    clients_.push_back({clientId, taskId, "", std::move(sendCallback), true});
    return clientId;
  }

  // --- 注销 SSE 客户端 ---
  void unregisterClient(const std::string &clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &client : clients_) {
      if (client.clientId == clientId) {
        client.connected = false;
        break;
      }
    }
    // 清理断开的连接
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
                       [](const SseClient &c) { return !c.connected; }),
        clients_.end());
  }

  // --- 发送历史事件（新连接重放）---
  void replayHistory(const std::string &clientId, const std::string &taskId) {
    auto events = eventBus_.getHistory(taskId);
    for (const auto &event : events) {
      sendToClient(clientId, event);
    }
  }

  // --- 获取活跃连接数 ---
  size_t activeConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
  }

private:
  EventBus &eventBus_;
  ListenerId listenerId_;
  mutable std::mutex mutex_;
  std::vector<SseClient> clients_;
  size_t nextClientId_{1};

  // 收到事件时的回调
  void onEvent(const EventData &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &client : clients_) {
      if (client.connected) {
        // 如果客户端订阅了该 task，或者是通配（空 taskId）
        if (client.taskId.empty() || client.taskId == event.taskId) {
          sendToClientInternal(client, event);
        }
      }
    }
  }

  // 向指定客户端发送事件
  void sendToClient(const std::string &clientId, const EventData &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &client : clients_) {
      if (client.clientId == clientId && client.connected) {
        sendToClientInternal(client, event);
        break;
      }
    }
  }

  // 内部发送（假设已持有锁）
  void sendToClientInternal(SseClient &client, const EventData &event) {
    if (!client.sendCallback)
      return;

    std::ostringstream sse;
    sse << "event: " << event.typeToString() << "\n";
    sse << "data: " << event.serialize() << "\n";
    sse << "\n";

    client.sendCallback(sse.str());
    client.lastEventId = event.id;
  }
};

// ============================================================
// 便捷工厂函数
// ============================================================
std::unique_ptr<EventDispatcher> createEventDispatcher(EventBus &eventBus) {
  return std::make_unique<EventDispatcher>(eventBus);
}

} // namespace codepilot
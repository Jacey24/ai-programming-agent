#include "EventDispatcher.h"

#include <sstream>

namespace codepilot {

// ============================================================
// EventDispatcher 实现
// ============================================================

EventDispatcher::EventDispatcher(EventBus& eventBus)
    : eventBus_(eventBus), listenerId_(0) {
    // 订阅所有事件
    listenerId_ = eventBus_.subscribeAll(
        [this](const EventData& event) { this->onEvent(event); });
}

EventDispatcher::~EventDispatcher() {
    eventBus_.unsubscribe(listenerId_);
}

std::string EventDispatcher::registerClient(
    const std::string& taskId,
    std::function<void(const std::string&)> sendCallback) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string clientId = "sse_" + std::to_string(nextClientId_++);
    clients_.push_back({clientId, taskId, "", std::move(sendCallback), true});
    return clientId;
}

void EventDispatcher::unregisterClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : clients_) {
        if (client.clientId == clientId) {
            client.connected = false;
            break;
        }
    }
    // 清理断开的连接
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
                       [](const SseClient& c) { return !c.connected; }),
        clients_.end());
}

void EventDispatcher::replayHistory(const std::string& clientId,
                                    const std::string& taskId) {
    auto events = eventBus_.getHistory(taskId);
    for (const auto& event : events) {
        sendToClient(clientId, event);
    }
}

size_t EventDispatcher::activeConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
}

void EventDispatcher::onEvent(const EventData& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : clients_) {
        if (client.connected) {
            // 如果客户端订阅了该 task，或者是通配（空 taskId）
            if (client.taskId.empty() || client.taskId == event.taskId) {
                sendToClientInternal(client, event);
            }
        }
    }
}

void EventDispatcher::sendToClient(const std::string& clientId,
                                   const EventData& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : clients_) {
        if (client.clientId == clientId && client.connected) {
            sendToClientInternal(client, event);
            break;
        }
    }
}

void EventDispatcher::sendToClientInternal(SseClient& client,
                                           const EventData& event) {
    if (!client.sendCallback) return;

    std::ostringstream sse;
    sse << "event: " << event.typeToString() << "\n";
    sse << "data: " << event.serialize() << "\n";
    sse << "\n";

    client.sendCallback(sse.str());
    client.lastEventId = event.id;
}

// ============================================================
// 便捷工厂函数
// ============================================================
std::unique_ptr<EventDispatcher> createEventDispatcher(EventBus& eventBus) {
    return std::make_unique<EventDispatcher>(eventBus);
}

} // namespace codepilot

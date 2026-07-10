#pragma once

#include "EventBus.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// SSE 连接客户端
// ============================================================
struct SseClient {
    std::string clientId;
    std::string taskId;
    std::string lastEventId;
    std::function<void(const std::string&)> sendCallback;
    bool connected{true};
};

// ============================================================
// EventDispatcher - SSE 事件分发器
// 负责将 EventBus 上的事件分发给对应的 SSE 连接
// ============================================================
class EventDispatcher {
public:
    explicit EventDispatcher(EventBus& eventBus);
    ~EventDispatcher();

    // 注册 SSE 客户端（连接到指定 task）
    std::string registerClient(
        const std::string& taskId,
        std::function<void(const std::string&)> sendCallback);

    // 注销 SSE 客户端
    void unregisterClient(const std::string& clientId);

    // 发送历史事件（新连接重放）
    void replayHistory(const std::string& clientId, const std::string& taskId);

    // 获取活跃连接数
    size_t activeConnections() const;

private:
    EventBus& eventBus_;
    ListenerId listenerId_;
    mutable std::mutex mutex_;
    std::vector<SseClient> clients_;
    size_t nextClientId_{1};

    void onEvent(const EventData& event);
    void sendToClient(const std::string& clientId, const EventData& event);
    void sendToClientInternal(SseClient& client, const EventData& event);
};

// 便捷工厂函数
std::unique_ptr<EventDispatcher> createEventDispatcher(EventBus& eventBus);

} // namespace codepilot

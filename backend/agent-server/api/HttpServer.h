#pragma once

#include <atomic>
#include <string>

namespace codepilot {

class EventBus;
class EventDispatcher;

struct HttpServerConfig {
    std::string host{"0.0.0.0"};
    int port{8080};
    std::string databasePath{"/data/agent.db"};
    bool databaseConnected{false};
    std::string databaseError;
    EventBus* eventBus{nullptr};
    EventDispatcher* eventDispatcher{nullptr};
};

class HttpServer {
public:
    explicit HttpServer(HttpServerConfig config);

    int run(const std::atomic_bool& running);

private:
    std::string handleRequest(const std::string& request);
    std::string healthResponse() const;
    std::string createChatResponse(const std::string& request) const;
    std::string chatHistoryResponse() const;

    // SSE 事件流处理
    void handleStreamRequest(int client_fd,
                             const std::string& request,
                             const std::atomic_bool& running);

    HttpServerConfig config_;
};

} // namespace codepilot

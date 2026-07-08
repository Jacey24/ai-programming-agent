#pragma once

#include <atomic>
#include <string>

namespace codepilot {

struct HttpServerConfig {
    std::string host{"0.0.0.0"};
    int port{8080};
    std::string databasePath{"/data/agent.db"};
    bool databaseConnected{false};
    std::string databaseError;
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

    HttpServerConfig config_;
};

} // namespace codepilot

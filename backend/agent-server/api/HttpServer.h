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

  int run(const std::atomic_bool &running);

private:
  std::string handleRequest(const std::string &request);
  std::string healthResponse() const;

  // SSE 实时事件流：保持 socket 长连接，回放历史事件并订阅 EventBus 实时推送
  void streamTaskEvents(int client_fd, const std::string &task_id);

  HttpServerConfig config_;
};

} // namespace codepilot

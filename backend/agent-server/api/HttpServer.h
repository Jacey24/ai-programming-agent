#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace codepilot {

struct HttpServerConfig {
  std::string host{"0.0.0.0"};
  int port{8080};
  std::string databasePath{"./storage/agent.db"};
  bool databaseConnected{false};
  std::string databaseError;
};

class HttpServer {
public:
  explicit HttpServer(HttpServerConfig config);
  ~HttpServer();

  // 返回 0 表示正常退出，非 0 表示启动失败
  int run(const std::atomic_bool &running);

  // healthResponse 供 registerAllRoutes 中 lambda 通过指针调用
  std::string healthResponse() const;

private:
  // PIMPL: 平台相关实现隐藏在 .cpp 中
  //   Linux: 手写裸 socket
  //   Windows: cpp-httplib (httplib::Server)
  struct Impl;
  std::unique_ptr<Impl> impl_;

#ifndef _WIN32
  // Linux 专用方法声明
  std::string handleRequest(const std::string &request);
  void streamTaskEvents(int client_fd, const std::string &task_id);
#endif

  HttpServerConfig config_;
};

} // namespace codepilot
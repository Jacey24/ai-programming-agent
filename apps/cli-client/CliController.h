#pragma once

#include "ApiClient.h"
#include "EventPrinter.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class CliController {
public:
  CliController(const std::string &host = "localhost", int port = 8080);

  int run();

private:
  void printBanner();
  void printHelp();
  std::string readLine();

  // 命令处理
  void handleTaskCreate(const std::string &args);
  void handleTaskCancel(const std::string &args);
  void handleTaskList();
  void handleTaskStatus(const std::string &args);
  void handleTaskHistory(const std::string &args);
  void handleTaskActive();
  void handleTaskDelete(const std::string &args);
  void handleTools();
  void handleExpertsList();
  void handleHealth();
  void handleVerbose(const std::string &args);

  // 工作区管理
  void handleWorkspaceCreate(const std::string &args);
  void handleWorkspaceList();
  void handleWorkspaceShow(const std::string &args);
  void handleWorkspaceUse(const std::string &args);
  void handleWorkspaceFiles(const std::string &args);
  void handleWorkspaceDelete(const std::string &args);

  // 对话管理
  void handleSessionCreate(const std::string &args);
  void handleSessionUpdate(const std::string &args);
  void handleSessionList();
  void handleSessionDelete(const std::string &args);

  // Global 上下文管理
  void handleGlobalList();
  void handleGlobalCreate(const std::string &args);
  void handleGlobalUse(const std::string &args);
  void handleGlobalShow(const std::string &args);
  void handleGlobalDelete(const std::string &args);

  // 权限管理
  void handlePermList(const std::string &taskId = "");
  void handlePermApprove(const std::string &args);
  void handlePermReject(const std::string &args);

  // SSE 流监听
  void startStreamListener(const std::string &taskId);
  void stopStreamListener();

  ApiClient client_;
  EventPrinter printer_;
  bool running_{true};
  bool verbose_{false};
  std::string workspacePath_;
  std::string activeWorkspaceId_;
  std::string activeSessionId_;
  std::string activeGlobalId_{"g_default"};

  std::atomic_bool streamCancelFlag_{false};
  std::thread streamThread_;
};

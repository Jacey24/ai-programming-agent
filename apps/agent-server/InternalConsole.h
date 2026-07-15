#pragma once

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

using json = nlohmann::json;

class InternalConsole {
public:
  InternalConsole(const std::string &host = "localhost", int port = 8080);
  ~InternalConsole();

  void start();
  void stop();

private:
  std::string doGet(const std::string &path);
  std::string doPost(const std::string &path, const std::string &body);
  std::string doPut(const std::string &path, const std::string &body);
  std::string doDelete(const std::string &path);

  json parseOrError(const std::string &resp, const std::string &op);
  void printError(const json &resp);

  void replLoop();
  std::string readLine();
  void printBanner();
  void printHelp();

  // Global (暂不可用)
  void handleGlobalList();
  void handleGlobalCreate(const std::string &args);
  void handleGlobalUse(const std::string &args);
  void handleGlobalShow(const std::string &args);
  void handleGlobalDelete(const std::string &args);

  // Task
  void handleTaskCreate(const std::string &args);
  void handleTaskCancel(const std::string &args);
  void handleTaskList();
  void handleTaskStatus(const std::string &args);
  void handleTaskHistory(const std::string &args);
  void handleTaskActive();
  void handleTaskDelete(const std::string &args);

  // Workspace
  void handleWorkspaceCreate(const std::string &args);
  void handleWorkspaceList();
  void handleWorkspaceShow(const std::string &args);
  void handleWorkspaceUse(const std::string &args);
  void handleWorkspaceUpdate(const std::string &args);
  void handleWorkspaceDelete(const std::string &args);
  void handleWorkspaceFiles(const std::string &args);
  void handleWorkspaceSessions(const std::string &args);

  // Session
  void handleSessionCreate(const std::string &args);
  void handleSessionList();
  void handleSessionUse(const std::string &args);
  void handleSessionUpdate(const std::string &args);
  void handleSessionDelete(const std::string &args);

  // Permission
  void handlePermList();
  void handlePermApprove(const std::string &args);
  void handlePermReject(const std::string &args);

  void handleTools();
  void handleExpertsList();
  void handleHealth();
  void handleVerbose(const std::string &args);

  void startStreamListener(const std::string &taskId);
  void stopStreamListener();
  void onSseEvent(const json &event);

  std::string host_;
  int port_;
  std::string baseUrl() const;

  std::atomic_bool running_{true};
  std::thread replThread_;

  std::string activeGlobalId_{"g_default"};
  std::string activeWorkspaceId_{"ws_default"};
  std::string activeSessionId_;
  std::string workspacePath_{"."};

  bool verbose_{false};

  std::atomic_bool streamCancelFlag_{false};
  std::thread streamThread_;
};
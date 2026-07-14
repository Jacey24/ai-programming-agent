#pragma once

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

using json = nlohmann::json;

/**
 * @brief Server 内部控制台 — 集成 CLI 测试环境
 *
 * 在 codepilot-agent-server 进程内部启动一个交互式 REPL，
 * 通过 httplib::Client 向自身发起 HTTP 请求，覆盖所有对外 API。
 * 命令行语法完全兼容 apps/cli-client。
 *
 * 用法：
 *   codepilot-agent-server.exe --console
 *   等待 HTTP Server 启动完成后，控制台出现 "> " 提示符。
 */
class InternalConsole {
public:
  InternalConsole(const std::string &host = "localhost", int port = 8080);
  ~InternalConsole();

  /** 启动 REPL 线程（非阻塞） */
  void start();

  /** 发送停止信号并等待线程结束 */
  void stop();

private:
  // ── HTTP 传输 ──
  std::string doGet(const std::string &path);
  std::string doPost(const std::string &path, const std::string &body);
  std::string doPut(const std::string &path, const std::string &body);
  std::string doDelete(const std::string &path);

  // ── JSON 辅助 ──
  json parseOrError(const std::string &resp, const std::string &op);
  void printError(const json &resp);

  // ── REPL ──
  void replLoop();
  std::string readLine();
  void printBanner();
  void printHelp();

  // ── 命令处理 ──
  void handleGlobalList();
  void handleGlobalCreate(const std::string &args);
  void handleGlobalUse(const std::string &args);
  void handleGlobalShow(const std::string &args);
  void handleGlobalDelete(const std::string &args);

  void handleTaskCreate(const std::string &args);
  void handleTaskCancel(const std::string &args);
  void handleTaskList();
  void handleTaskStatus(const std::string &args);
  void handleTaskHistory(const std::string &args);
  void handleTaskActive();
  void handleTaskDelete(const std::string &args);

  void handleWorkspaceCreate(const std::string &args);
  void handleWorkspaceList();
  void handleWorkspaceShow(const std::string &args);
  void handleWorkspaceUse(const std::string &args);
  void handleWorkspaceDelete(const std::string &args);
  void handleWorkspaceFiles(const std::string &args);

  void handleSessionCreate(const std::string &args);
  void handleSessionList();
  void handleSessionUpdate(const std::string &args);
  void handleSessionDelete(const std::string &args);

  void handlePermList();
  void handlePermApprove(const std::string &args);
  void handlePermReject(const std::string &args);

  void handleTools();
  void handleExpertsList();
  void handleHealth();
  void handleVerbose(const std::string &args);

  // ── SSE 流监听 ──
  void startStreamListener(const std::string &taskId);
  void stopStreamListener();

  // ── SSE 事件打印 ──
  void onSseEvent(const json &event);

  // ── 状态 ──
  std::string host_;
  int port_;
  std::string baseUrl() const;

  std::atomic_bool running_{true};
  std::thread replThread_;

  std::string activeGlobalId_{"g_default"};
  std::string activeWorkspaceId_{"ws_default"};
  std::string workspacePath_{"."};

  bool verbose_{false};

  // SSE
  std::atomic_bool streamCancelFlag_{false};
  std::thread streamThread_;
};
#include "InternalConsole.h"
#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

// ============================================================
// 构造 / 析构
// ============================================================

InternalConsole::InternalConsole(const std::string &host, int port)
    : host_(host), port_(port) {}

InternalConsole::~InternalConsole() { stop(); }

std::string InternalConsole::baseUrl() const {
  return host_ + ":" + std::to_string(port_);
}

// ============================================================
// HTTP 传输（内部 httplib::Client → 自己）
// ============================================================

std::string InternalConsole::doGet(const std::string &path) {
  if (verbose_) {
    std::cout << "  [HTTP] GET " << path << std::endl;
  }
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Get(path);
  if (!res) {
    if (verbose_)
      std::cerr << "  [HTTP] GET FAILED" << std::endl;
    return "";
  }
  return res->body;
}

std::string InternalConsole::doPost(const std::string &path,
                                    const std::string &body) {
  if (verbose_) {
    std::cout << "  [HTTP] POST " << path << " body=" << body.substr(0, 200)
              << std::endl;
  }
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Post(path, body, "application/json");
  if (!res) {
    if (verbose_)
      std::cerr << "  [HTTP] POST FAILED" << std::endl;
    return "";
  }
  return res->body;
}

std::string InternalConsole::doPut(const std::string &path,
                                   const std::string &body) {
  if (verbose_) {
    std::cout << "  [HTTP] PUT " << path << " body=" << body.substr(0, 200)
              << std::endl;
  }
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Put(path, body, "application/json");
  if (!res)
    return "";
  return res->body;
}

std::string InternalConsole::doDelete(const std::string &path) {
  if (verbose_) {
    std::cout << "  [HTTP] DELETE " << path << std::endl;
  }
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Delete(path);
  if (!res)
    return "";
  return res->body;
}

// ============================================================
// JSON 辅助
// ============================================================

json InternalConsole::parseOrError(const std::string &resp,
                                   const std::string &op) {
  if (resp.empty()) {
    json err;
    err["success"] = false;
    err["error"] = {{"message", op + ": 无响应"}};
    return err;
  }
  try {
    return json::parse(resp);
  } catch (const std::exception &e) {
    json err;
    err["success"] = false;
    err["error"] = {{"message", op + ": 解析失败 - " + std::string(e.what())}};
    return err;
  }
}

void InternalConsole::printError(const json &resp) {
  if (resp.contains("error")) {
    auto &err = resp["error"];
    if (err.is_object()) {
      std::cout << "  [错误] "
                << err.value("message", err.value("code", "未知错误"))
                << std::endl;
    } else {
      std::cout << "  [错误] " << err.dump() << std::endl;
    }
  } else {
    std::cout << "  [错误] " << resp.dump().substr(0, 200) << std::endl;
  }
}

// ============================================================
// REPL 主循环
// ============================================================

void InternalConsole::start() {
  replThread_ = std::thread(&InternalConsole::replLoop, this);
}

void InternalConsole::stop() {
  running_.store(false);
  stopStreamListener();
  if (replThread_.joinable()) {
    replThread_.join();
  }
}

std::string InternalConsole::readLine() {
  std::cout << "> ";
  std::string line;
  std::getline(std::cin, line);
  return line;
}

void InternalConsole::printBanner() {
  std::cout << "\n"
            << "  [InternalConsole] Server 内建调试控制台\n"
            << "  Server: http://" << baseUrl() << "\n"
            << "  Global: " << activeGlobalId_
            << "  Workspace: " << activeWorkspaceId_ << "\n"
            << "  输入 /help 查看命令，/quit 退出\n"
            << std::endl;
}

void InternalConsole::printHelp() {
  std::cout << "\n  === 上下文 ===" << std::endl;
  std::cout << "    /global list                  列出所有 Global" << std::endl;
  std::cout << "    /global create <name>         创建新 Global" << std::endl;
  std::cout << "    /global use <id>              切换当前 Global" << std::endl;
  std::cout << "    /global show [id]             查看 Global 详情"
            << std::endl;
  std::cout << "    /global delete <id>           删除 Global" << std::endl;
  std::cout << "\n  === 工作区 ===" << std::endl;
  std::cout << "    /workspace create <name> <path>  创建工作区" << std::endl;
  std::cout << "    /workspace list                  列出工作区" << std::endl;
  std::cout << "    /workspace show [id]             查看工作区" << std::endl;
  std::cout << "    /workspace use <id>              切换工作区" << std::endl;
  std::cout << "    /workspace delete <id>           删除工作区" << std::endl;
  std::cout << "    /workspace files [id]           罗列文件树" << std::endl;
  std::cout << "\n  === 任务 ===" << std::endl;
  std::cout
      << "    /task create <goal>          创建并执行任务（进入 SSE 监听）"
      << std::endl;
  std::cout << "    /task cancel <id>            取消任务" << std::endl;
  std::cout << "    /task list                   列出最近任务" << std::endl;
  std::cout << "    /task status <id>            查看任务详情" << std::endl;
  std::cout << "    /task history <id>           查看任务事件历史" << std::endl;
  std::cout << "    /task active                 查看活跃任务" << std::endl;
  std::cout << "    /task delete <id>            删除已完成/失败的任务"
            << std::endl;
  std::cout << "\n  === 对话 ===" << std::endl;
  std::cout << "    /session create <title>      创建对话" << std::endl;
  std::cout << "    /session list                列出对话" << std::endl;
  std::cout << "    /session update <id> k=v..   更新对话" << std::endl;
  std::cout << "    /session delete <id>         删除对话" << std::endl;
  std::cout << "\n  === 权限 ===" << std::endl;
  std::cout << "    /perm list                   列出待处理权限" << std::endl;
  std::cout << "    /perm approve [id]           批准权限" << std::endl;
  std::cout << "    /perm reject <id>            拒绝权限" << std::endl;
  std::cout << "\n  === 其他 ===" << std::endl;
  std::cout << "    /tools                       列出可用工具" << std::endl;
  std::cout << "    /experts list                列出 Expert Chain"
            << std::endl;
  std::cout << "    /health                      健康检查" << std::endl;
  std::cout << "    /verbose on|off              开关调试日志" << std::endl;
  std::cout << "    /help                        显示此帮助" << std::endl;
  std::cout << "    /quit                        退出控制台" << std::endl;
}

void InternalConsole::replLoop() {
  // 等待 HTTP Server 就绪
  bool serverReady = false;
  for (int retry = 0; retry < 30; ++retry) {
    std::string resp = doGet("/health");
    if (!resp.empty() && (resp.find("\"status\":\"ok\"") != std::string::npos ||
                          resp.find("OK") != std::string::npos)) {
      serverReady = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  if (!serverReady) {
    std::cout << "  [警告] HTTP Server 未就绪，命令可能失败" << std::endl;
  }

  printBanner();

  while (running_) {
    std::string input = readLine();
    if (input.empty())
      continue;

    if (input == "/quit") {
      stopStreamListener();
      running_ = false;
      break;
    }

    // ── 命令分发 ──

    // Global
    if (input == "/global list") {
      handleGlobalList();
    } else if (input.rfind("/global create ", 0) == 0) {
      handleGlobalCreate(input.substr(15));
    } else if (input.rfind("/global use ", 0) == 0) {
      handleGlobalUse(input.substr(12));
    } else if (input.rfind("/global delete ", 0) == 0) {
      handleGlobalDelete(input.substr(15));
    } else if (input.rfind("/global show ", 0) == 0) {
      handleGlobalShow(input.substr(13));
    } else if (input == "/global show") {
      handleGlobalShow("");
    }
    // Task
    else if (input.rfind("/task create ", 0) == 0) {
      handleTaskCreate(input.substr(13));
    } else if (input.rfind("/task cancel ", 0) == 0) {
      handleTaskCancel(input.substr(13));
    } else if (input.rfind("/task delete ", 0) == 0) {
      handleTaskDelete(input.substr(13));
    } else if (input == "/task list") {
      handleTaskList();
    } else if (input.rfind("/task status ", 0) == 0) {
      handleTaskStatus(input.substr(13));
    } else if (input.rfind("/task history ", 0) == 0) {
      handleTaskHistory(input.substr(14));
    } else if (input == "/task active") {
      handleTaskActive();
    }
    // Workspace
    else if (input.rfind("/workspace create ", 0) == 0) {
      handleWorkspaceCreate(input.substr(18));
    } else if (input == "/workspace list") {
      handleWorkspaceList();
    } else if (input.rfind("/workspace show ", 0) == 0) {
      handleWorkspaceShow(input.substr(16));
    } else if (input == "/workspace show") {
      handleWorkspaceShow("");
    } else if (input.rfind("/workspace use ", 0) == 0) {
      handleWorkspaceUse(input.substr(15));
    } else if (input.rfind("/workspace delete ", 0) == 0) {
      handleWorkspaceDelete(input.substr(18));
    } else if (input.rfind("/workspace files ", 0) == 0) {
      handleWorkspaceFiles(input.substr(17));
    } else if (input == "/workspace files") {
      handleWorkspaceFiles("");
    }
    // Session
    else if (input.rfind("/session create ", 0) == 0) {
      handleSessionCreate(input.substr(16));
    } else if (input == "/session list") {
      handleSessionList();
    } else if (input.rfind("/session update ", 0) == 0) {
      handleSessionUpdate(input.substr(16));
    } else if (input.rfind("/session delete ", 0) == 0) {
      handleSessionDelete(input.substr(16));
    }
    // Permission
    else if (input == "/perm list") {
      handlePermList();
    } else if (input == "/perm approve") {
      handlePermApprove("");
    } else if (input.rfind("/perm approve ", 0) == 0) {
      handlePermApprove(input.substr(14));
    } else if (input.rfind("/perm reject ", 0) == 0) {
      handlePermReject(input.substr(13));
    }
    // Other
    else if (input == "/tools") {
      handleTools();
    } else if (input == "/experts list") {
      handleExpertsList();
    } else if (input == "/health") {
      handleHealth();
    } else if (input.rfind("/verbose ", 0) == 0) {
      handleVerbose(input.substr(9));
    } else if (input == "/help") {
      printHelp();
    } else {
      std::cout << "  未知命令。输入 /help 查看命令列表" << std::endl;
    }
  }

  std::cout << "  控制台已退出" << std::endl;
}

// ============================================================
// Global 命令
// ============================================================

void InternalConsole::handleGlobalList() {
  auto resp = parseOrError(doGet("/api/v1/globals"), "listGlobals");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无 Global)" << std::endl;
    return;
  }
  for (const auto &g : items) {
    std::string marker =
        (g.value("id", "") == activeGlobalId_) ? " ← 当前" : "";
    std::cout << "  " << g.value("id", "?") << marker << "  "
              << g.value("name", "?") << std::endl;
  }
}

void InternalConsole::handleGlobalCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global create <名称>" << std::endl;
    return;
  }
  json body;
  body["name"] = args;
  auto resp =
      parseOrError(doPost("/api/v1/globals", body.dump()), "createGlobal");
  if (resp.value("success", false)) {
    std::cout << "  Global 已创建: " << resp["data"].value("id", "")
              << std::endl;
  } else {
    printError(resp);
  }
}

void InternalConsole::handleGlobalUse(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global use <global_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(doGet("/api/v1/globals/" + args), "getGlobal");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  activeGlobalId_ = args;
  std::cout << "  已切换到 Global: " << activeGlobalId_ << std::endl;
}

void InternalConsole::handleGlobalShow(const std::string &args) {
  std::string id = args.empty() ? activeGlobalId_ : args;
  auto resp = parseOrError(doGet("/api/v1/globals/" + id), "getGlobal");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto &data = resp["data"];
  std::cout << "  Global: " << data.value("id", "") << std::endl;
  std::cout << "  名称: " << data.value("name", "") << std::endl;
  std::cout << "  创建时间: " << data.value("created_at", "") << std::endl;
}

void InternalConsole::handleGlobalDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global delete <global_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(doDelete("/api/v1/globals/" + args), "deleteGlobal");
  if (resp.value("success", false)) {
    std::cout << "  Global " << args << " 已删除" << std::endl;
    if (args == activeGlobalId_)
      activeGlobalId_ = "g_default";
  } else {
    printError(resp);
  }
}

// ============================================================
// 任务命令
// ============================================================

void InternalConsole::handleTaskCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task create <目标描述>" << std::endl;
    return;
  }
  std::cout << "  [创建任务] " << args << std::endl;
  json body;
  body["input"] = args;
  body["global_id"] = activeGlobalId_;
  body["workspace_id"] = activeWorkspaceId_;
  auto resp = parseOrError(doPost("/api/v1/tasks", body.dump()), "createTask");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  std::string taskId = resp["data"].value("id", "");
  std::string status = resp["data"].value("status", "");
  std::cout << "  任务 ID: " << taskId << "  状态: " << status << std::endl;
  if (status == "running") {
    std::cout << "  [SSE 监听中...]\n" << std::endl;
    startStreamListener(taskId);
  }
}

void InternalConsole::handleTaskCancel(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task cancel <task_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(doPost("/api/v1/tasks/" + args + "/cancel", "{}"),
                           "cancelTask");
  if (resp.value("success", false)) {
    std::cout << "  任务 " << args << " 已取消" << std::endl;
  } else {
    printError(resp);
  }
}

void InternalConsole::handleTaskList() {
  auto resp = parseOrError(doGet("/api/v1/tasks"), "listTasks");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无任务)" << std::endl;
    return;
  }
  for (const auto &t : items) {
    std::cout << "  " << t.value("id", "?") << "  [" << t.value("status", "?")
              << "]  " << t.value("goal", "").substr(0, 50) << std::endl;
  }
}

void InternalConsole::handleTaskStatus(const std::string &args) {
  auto resp = parseOrError(doGet("/api/v1/tasks/" + args), "getTask");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto &data = resp["data"];
  std::cout << "  ID: " << data.value("id", "") << std::endl;
  std::cout << "  状态: " << data.value("status", "") << std::endl;
  std::cout << "  目标: " << data.value("goal", "") << std::endl;
}

void InternalConsole::handleTaskHistory(const std::string &args) {
  auto resp = parseOrError(doGet("/api/v1/tasks/" + args + "/events/history"),
                           "getTaskHistory");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无历史事件)" << std::endl;
    return;
  }
  for (const auto &ev : items) {
    std::string type = ev.value("type", "?");
    std::string content = ev.value("content", "").substr(0, 60);
    std::cout << "  [" << type << "] " << content << std::endl;
  }
}

void InternalConsole::handleTaskActive() {
  auto resp = parseOrError(doGet("/api/v1/tasks/active"), "listActiveTasks");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (无活跃任务)" << std::endl;
    return;
  }
  for (const auto &t : items) {
    std::cout << "  " << t.value("task_id", "?") << "  ["
              << t.value("status", "?") << "]  "
              << t.value("goal", "").substr(0, 50) << std::endl;
  }
}

void InternalConsole::handleTaskDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task delete <task_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(doDelete("/api/v1/tasks/" + args), "deleteTask");
  if (resp.value("success", false)) {
    std::cout << "  任务 " << args << " 已删除" << std::endl;
  } else {
    printError(resp);
  }
}

// ============================================================
// 工作区命令
// ============================================================

void InternalConsole::handleWorkspaceCreate(const std::string &args) {
  auto spacePos = args.find(' ');
  if (spacePos == std::string::npos) {
    std::cout << "  用法: /workspace create <name> <path>" << std::endl;
    return;
  }
  std::string name = args.substr(0, spacePos);
  std::string path = args.substr(spacePos + 1);
  json body;
  body["name"] = name;
  body["path"] = path;
  auto resp = parseOrError(doPost("/api/v1/workspaces", body.dump()),
                           "createWorkspace");
  if (resp.value("success", false)) {
    auto &data = resp["data"];
    std::cout << "  工作区已创建: " << data.value("id", "?") << " ("
              << data.value("name", "") << ")" << std::endl;
    workspacePath_ = path;
  } else {
    printError(resp);
  }
}

void InternalConsole::handleWorkspaceList() {
  auto resp = parseOrError(doGet("/api/v1/workspaces"), "listWorkspaces");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无工作区)" << std::endl;
    return;
  }
  for (const auto &w : items) {
    std::string marker =
        (w.value("id", "") == activeWorkspaceId_) ? " ← 当前" : "";
    std::cout << "  " << w.value("id", "?") << marker << "  "
              << w.value("name", "?") << "  -> " << w.value("path", "?")
              << std::endl;
  }
}

void InternalConsole::handleWorkspaceShow(const std::string &args) {
  std::string id = args.empty() ? activeWorkspaceId_ : args;
  auto resp = parseOrError(doGet("/api/v1/workspaces/" + id), "getWorkspace");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto &data = resp["data"];
  std::cout << "  Workspace: " << data.value("id", "") << std::endl;
  std::cout << "  名称: " << data.value("name", "") << std::endl;
  std::cout << "  路径: " << data.value("path", "") << std::endl;
}

void InternalConsole::handleWorkspaceUse(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /workspace use <workspace_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(doGet("/api/v1/workspaces/" + args), "getWorkspace");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  activeWorkspaceId_ = args;
  workspacePath_ = resp["data"].value("path", "");
  std::cout << "  已切换到工作区: " << activeWorkspaceId_ << std::endl;
}

void InternalConsole::handleWorkspaceDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /workspace delete <workspace_id>" << std::endl;
    return;
  }
  auto resp =
      parseOrError(doDelete("/api/v1/workspaces/" + args), "deleteWorkspace");
  if (resp.value("success", false)) {
    std::cout << "  工作区 " << args << " 已删除" << std::endl;
    if (args == activeWorkspaceId_)
      activeWorkspaceId_ = "ws_default";
  } else {
    printError(resp);
  }
}

void InternalConsole::handleWorkspaceFiles(const std::string &args) {
  std::string id = args.empty() ? activeWorkspaceId_ : args;
  auto resp = parseOrError(doGet("/api/v1/workspaces/" + id + "/files/tree"),
                           "getFileTree");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto &data = resp["data"];
  std::string tree = data.value("tree", data.dump());
  std::cout << "  文件树:\n" << tree << std::endl;
}

// ============================================================
// 对话命令
// ============================================================

void InternalConsole::handleSessionCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /session create <标题>" << std::endl;
    return;
  }
  json body;
  body["title"] = args;
  auto resp =
      parseOrError(doPost("/api/v1/sessions", body.dump()), "createSession");
  if (resp.value("success", false)) {
    std::cout << "  对话已创建: " << resp["data"].value("id", "?") << std::endl;
  } else {
    printError(resp);
  }
}

void InternalConsole::handleSessionList() {
  auto resp = parseOrError(doGet("/api/v1/sessions"), "listSessions");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无对话)" << std::endl;
    return;
  }
  for (const auto &s : items) {
    std::cout << "  " << s.value("id", "?") << "  " << s.value("title", "?")
              << std::endl;
  }
}

void InternalConsole::handleSessionUpdate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /session update <id> title=xxx" << std::endl;
    return;
  }
  auto spacePos = args.find(' ');
  std::string id =
      (spacePos == std::string::npos) ? args : args.substr(0, spacePos);
  std::string kvArgs =
      (spacePos == std::string::npos) ? "" : args.substr(spacePos + 1);
  json body;
  auto eqPos = kvArgs.find('=');
  if (eqPos != std::string::npos) {
    std::string key = kvArgs.substr(0, eqPos);
    std::string val = kvArgs.substr(eqPos + 1);
    if (key == "title")
      body["title"] = val;
  }
  auto resp = parseOrError(doPut("/api/v1/sessions/" + id, body.dump()),
                           "updateSession");
  if (resp.value("success", false)) {
    std::cout << "  对话已更新" << std::endl;
  } else {
    printError(resp);
  }
}

void InternalConsole::handleSessionDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /session delete <session_id>" << std::endl;
    return;
  }
  auto resp =
      parseOrError(doDelete("/api/v1/sessions/" + args), "deleteSession");
  if (resp.value("success", false)) {
    std::cout << "  对话 " << args << " 已删除" << std::endl;
  } else {
    printError(resp);
  }
}

// ============================================================
// 权限命令
// ============================================================

void InternalConsole::handlePermList() {
  auto resp = parseOrError(doGet("/api/v1/permissions"), "listPermissions");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (无待处理权限)" << std::endl;
    return;
  }
  for (const auto &p : items) {
    std::cout << "  " << p.value("id", "?") << "  " << p.value("tool_name", "?")
              << "  状态: " << p.value("status", "?") << std::endl;
  }
}

void InternalConsole::handlePermApprove(const std::string &args) {
  if (args.empty()) {
    auto resp = parseOrError(doPost("/api/v1/permissions/approve-first", "{}"),
                             "approveFirst");
    if (resp.value("success", false))
      std::cout << "  已批准" << std::endl;
    else
      printError(resp);
    return;
  }
  auto resp = parseOrError(
      doPost("/api/v1/permissions/" + args + "/approve", "{}"), "approvePerm");
  if (resp.value("success", false))
    std::cout << "  权限 " << args << " 已批准" << std::endl;
  else
    printError(resp);
}

void InternalConsole::handlePermReject(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /perm reject <permission_id>" << std::endl;
    return;
  }
  auto resp = parseOrError(
      doPost("/api/v1/permissions/" + args + "/reject", "{}"), "rejectPerm");
  if (resp.value("success", false))
    std::cout << "  权限 " << args << " 已拒绝" << std::endl;
  else
    printError(resp);
}

// ============================================================
// 查询命令
// ============================================================

void InternalConsole::handleTools() {
  auto resp = parseOrError(doGet("/api/v1/tools"), "listTools");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  json tools;
  if (resp.contains("data") && resp["data"].contains("items"))
    tools = resp["data"]["items"];
  else if (resp.contains("data") && resp["data"].contains("tools"))
    tools = resp["data"]["tools"];
  else if (resp.contains("data") && resp["data"].is_array())
    tools = resp["data"];
  if (tools.empty()) {
    std::cout << "  (暂无工具)" << std::endl;
    return;
  }
  for (const auto &t : tools) {
    std::cout << "  " << t.value("name", "?") << " - "
              << t.value("description", "").substr(0, 60) << std::endl;
  }
}

void InternalConsole::handleExpertsList() {
  auto resp = parseOrError(doGet("/api/v1/experts"), "listExperts");
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  json experts;
  if (resp.contains("data") && resp["data"].contains("items"))
    experts = resp["data"]["items"];
  else if (resp.contains("data") && resp["data"].is_array())
    experts = resp["data"];
  if (experts.empty()) {
    std::cout << "  (暂无 Expert)" << std::endl;
    return;
  }
  for (const auto &e : experts) {
    std::cout << "  " << e.value("name", "?")
              << (e.value("is_entry", false) ? " [入口]" : "") << std::endl;
  }
}

void InternalConsole::handleHealth() {
  std::string resp = doGet("/health");
  bool ok =
      (!resp.empty() && (resp.find("\"status\":\"ok\"") != std::string::npos ||
                         resp.find("OK") != std::string::npos));
  std::cout << "  服务器状态: " << (ok ? "正常" : "异常") << std::endl;
}

void InternalConsole::handleVerbose(const std::string &args) {
  if (args == "on") {
    verbose_ = true;
    std::cout << "  调试日志: 开启" << std::endl;
  } else {
    verbose_ = false;
    std::cout << "  调试日志: 关闭" << std::endl;
  }
}

// ============================================================
// SSE 流监听
// ============================================================

void InternalConsole::startStreamListener(const std::string &taskId) {
  stopStreamListener();
  streamCancelFlag_.store(false);
  streamThread_ = std::thread([this, taskId]() {
    httplib::Client cli(baseUrl());
    cli.set_connection_timeout(10);
    cli.set_read_timeout(300, 0);
    cli.set_write_timeout(300, 0);
    cli.set_keep_alive(true);

    std::string path = "/api/v1/tasks/" + taskId + "/events";
    std::string lineBuffer;

    auto receiver = [&](const char *data, size_t len) -> bool {
      if (streamCancelFlag_.load())
        return false;
      lineBuffer.append(data, len);

      size_t pos;
      while ((pos = lineBuffer.find('\n')) != std::string::npos) {
        std::string line = lineBuffer.substr(0, pos);
        lineBuffer.erase(0, pos + 1);

        // 去掉末尾的 \r
        if (!line.empty() && line.back() == '\r')
          line.pop_back();

        if (line.empty())
          continue; // 空行（SSE 边界）
        if (line.rfind("data: ", 0) == 0) {
          std::string content = line.substr(6);
          try {
            json event = json::parse(content);
            onSseEvent(event);
          } catch (...) {
            std::cout << "\n  [SSE-RAW] " << content.substr(0, 100)
                      << std::endl;
          }
        }
      }
      return true;
    };

    auto res = cli.Get(
        path, httplib::Headers(),
        [&](const httplib::Response &r) {
          if (r.status != 200) {
            std::cout << "\n  [SSE] 连接失败 status=" << r.status << std::endl;
            return false;
          }
          return true;
        },
        receiver);
    if (!res) {
      if (!streamCancelFlag_.load()) {
        std::cout << "\n  [SSE 流已结束]\n> " << std::flush;
      }
    }
  });
}

void InternalConsole::stopStreamListener() {
  streamCancelFlag_.store(true);
  if (streamThread_.joinable()) {
    streamThread_.join();
  }
}

void InternalConsole::onSseEvent(const json &event) {
  std::string type = event.value("type", "?");
  std::string content = event.value("content", "");
  std::string channel = "status";

  if (event.contains("metadata") && event["metadata"].contains("channel")) {
    channel = event["metadata"]["channel"].get<std::string>();
  }

  // 精简 SSE 输出
  if (channel == "dialog" && !content.empty()) {
    std::cout << "\n  💬 " << content.substr(0, 200) << std::endl
              << "> " << std::flush;
  } else if (type == "task_created") {
    std::cout << "\n  📋 任务已创建: " << event.value("task_id", "")
              << std::endl
              << "> " << std::flush;
  } else if (type == "task_completed") {
    std::cout << "\n  ✅ 任务完成" << std::endl << "> " << std::flush;
  } else if (type == "task_failed") {
    std::cout << "\n  ❌ 任务失败" << std::endl << "> " << std::flush;
  } else if (type == "task_cancelled") {
    std::cout << "\n  ⏹ 任务已取消" << std::endl << "> " << std::flush;
  } else if (type == "stream_end") {
    std::cout << "\n  [SSE 流已结束]\n> " << std::flush;
  } else {
    if (verbose_) {
      std::cout << "\n  [SSE/" << type << "] " << content.substr(0, 80)
                << std::endl
                << "> " << std::flush;
    }
  }
}
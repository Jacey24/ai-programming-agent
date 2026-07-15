#include "CliController.h"

#include <algorithm>
#include <iostream>
#include <thread>

CliController::CliController(const std::string &host, int port)
    : client_(host, port), printer_(false) {}

void CliController::printBanner() {
  std::cout << "\n"
            << "  ╔══════════════════════════════════╗\n"
            << "  ║       CodePilot CLI v0.6         ║\n"
            << "  ║  Server: http://" << client_.baseUrl();
  // pad to 34 chars width
  size_t urlLen = 7 + client_.baseUrl().size(); // "http://" + baseUrl
  for (size_t i = urlLen; i < 34; ++i)
    std::cout << " ";
  std::cout << "║\n"
            << "  ║  Global: " << activeGlobalId_ << "                  ║\n"
            << "  ║  Workspace: " << activeWorkspaceId_ << "               ║\n"
            << "  ╚══════════════════════════════════╝\n"
            << "  输入 /help 查看命令，/quit 退出\n"
            << std::endl;
}

void CliController::printHelp() {
  std::cout << "\n  === 上下文 ===" << std::endl;
  std::cout << "    /global list                  列出所有 Global" << std::endl;
  std::cout << "    /global create <name>         创建新 Global" << std::endl;
  std::cout << "    /global use <id>              切换当前 Global" << std::endl;
  std::cout << "    /global delete <id>            删除 Global (需确认)"
            << std::endl;
  std::cout << "    /global show [id]             查看 Global 详情/知识归档"
            << std::endl;
  std::cout << "\n  === 工作区 ===" << std::endl;
  std::cout << "    /workspace create <name> <path>  创建工作区" << std::endl;
  std::cout << "    /workspace list                  列出工作区" << std::endl;
  std::cout << "    /workspace show [id]             查看工作区 (无参=当前)"
            << std::endl;
  std::cout << "    /workspace use <id>              切换工作区" << std::endl;
  std::cout << "    /workspace delete <id>           删除工作区" << std::endl;
  std::cout << "    /workspace files [id]           罗列文件树 (无参=当前)"
            << std::endl;
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
  std::cout << "    /session create <title>      创建对话 (支持alias)"
            << std::endl;
  std::cout << "    /session list                列出对话" << std::endl;
  std::cout
      << "    /session update <id> <k=v..>  更新对话 title/alias/workspace"
      << std::endl;
  std::cout << "    /session delete <id>         删除对话" << std::endl;
  std::cout << "\n  === 权限 ===" << std::endl;
  std::cout << "    /perm list [task_id]         列出待处理权限" << std::endl;
  std::cout << "    /perm approve [id]           批准权限 (无参=首个pending)"
            << std::endl;
  std::cout << "    /perm reject <id>            拒绝权限" << std::endl;
  std::cout << "\n  === 其他 ===" << std::endl;
  std::cout << "    /tools                       列出可用工具" << std::endl;
  std::cout << "    /experts list                列出 Expert Chain"
            << std::endl;
  std::cout << "    /health                      健康检查" << std::endl;
  std::cout << "    /verbose on|off              开关调试日志" << std::endl;
  std::cout << "    /help                        显示此帮助" << std::endl;
  std::cout << "    /quit                        退出\n" << std::endl;
}

std::string CliController::readLine() {
  std::cout << "> ";
  std::string line;
  std::getline(std::cin, line);
  return line;
}

int CliController::run() {
  printBanner();

  if (!client_.healthCheck()) {
    std::cout << "  [错误] 无法连接到服务器 http://" << client_.baseUrl()
              << std::endl;
    std::cout << "  请先启动 codepilot-agent-server\n" << std::endl;
    return 1;
  }
  std::cout << "  [OK] 服务器连接正常\n" << std::endl;

  while (running_) {
    std::string input = readLine();
    if (input.empty())
      continue;

    if (input == "/quit") {
      stopStreamListener();
      running_ = false;
      break;
    }

    // Global commands
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
    // Task commands
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
    // Workspace commands
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
    // Session commands
    else if (input.rfind("/session create ", 0) == 0) {
      handleSessionCreate(input.substr(16));
    } else if (input == "/session list") {
      handleSessionList();
    } else if (input.rfind("/session update ", 0) == 0) {
      handleSessionUpdate(input.substr(16));
    } else if (input.rfind("/session delete ", 0) == 0) {
      handleSessionDelete(input.substr(16));
    }
    // Permission commands
    else if (input == "/perm list") {
      handlePermList();
    } else if (input.rfind("/perm list ", 0) == 0) {
      handlePermList(input.substr(11));
    } else if (input == "/perm approve") {
      handlePermApprove("");
    } else if (input.rfind("/perm approve ", 0) == 0) {
      handlePermApprove(input.substr(14));
    } else if (input.rfind("/perm reject ", 0) == 0) {
      handlePermReject(input.substr(13));
    }
    // Other commands
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

  std::cout << "  再见!" << std::endl;
  return 0;
}

// ── 错误输出辅助 ──
static void printError(const json &resp) {
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
    std::cout << "  [错误] 请求失败: " << resp.dump().substr(0, 200)
              << std::endl;
  }
}

// ── Global 命令 ──

void CliController::handleGlobalList() {
  auto resp = client_.listGlobals();
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无 Global，自动使用 g_default)" << std::endl;
    return;
  }
  for (const auto &g : items) {
    std::string marker =
        (g.value("id", "") == activeGlobalId_) ? " ← 当前" : "";
    std::string ws = g.value("workspace_id", "");
    std::cout << "  " << g.value("id", "?") << marker << "  "
              << g.value("name", "?") << std::endl;
    if (!ws.empty()) {
      std::cout << "    workspace: " << ws << std::endl;
    }
    std::string desc = g.value("description", "");
    if (!desc.empty()) {
      std::cout << "    " << desc.substr(0, 60) << std::endl;
    }
  }
}

void CliController::handleGlobalCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global create <名称>" << std::endl;
    return;
  }
  auto resp = client_.createGlobal(args);
  if (resp.value("success", false)) {
    auto data = resp["data"];
    std::cout << "  Global 已创建: " << data.value("id", "") << " ("
              << data.value("name", "") << ")" << std::endl;
  } else {
    printError(resp);
  }
}

void CliController::handleGlobalUse(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global use <global_id>" << std::endl;
    std::cout << "  当前: " << activeGlobalId_ << std::endl;
    return;
  }
  auto resp = client_.getGlobal(args);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  activeGlobalId_ = args;
  std::string wsId = resp["data"].value("workspace_id", "");
  std::cout << "  已切换到 Global: " << activeGlobalId_ << std::endl;
  std::cout << "   名称: " << resp["data"].value("name", "?") << std::endl;
  if (!wsId.empty() && wsId != activeWorkspaceId_) {
    activeSessionId_.clear();
    activeWorkspaceId_ = wsId;
    std::cout << "   关联工作区已自动切换: " << wsId << std::endl;
    std::cout << "   当前活动 Session 已清除" << std::endl;
  }
}

void CliController::handleGlobalShow(const std::string &args) {
  std::string id = args.empty() ? activeGlobalId_ : args;
  auto resp = client_.getGlobal(id);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto data = resp["data"];
  std::cout << "  Global: " << data.value("id", "") << std::endl;
  std::cout << "  名称: " << data.value("name", "") << std::endl;
  std::string ws = data.value("workspace_id", "");
  if (!ws.empty())
    std::cout << "  关联工作区: " << ws << std::endl;
  std::cout << "  创建时间: " << data.value("created_at", "") << std::endl;

  // 知识归档
  auto ctxResp = client_.getGlobalContext(id);
  if (ctxResp.value("success", false)) {
    auto items = ctxResp["data"]["items"];
    if (!items.empty()) {
      std::cout << "\n  ── 知识归档 (最近 3 条) ──" << std::endl;
      int count = 0;
      for (const auto &item : items) {
        if (++count > 3)
          break;
        std::string type = item.value("type", "?");
        std::string content = item.value("content", "");
        std::cout << "  [" << type << "] " << content.substr(0, 80)
                  << std::endl;
      }
    }
  }
}

void CliController::handleGlobalDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /global delete <global_id>" << std::endl;
    return;
  }
  auto resp = client_.deleteGlobal(args);
  if (resp.value("success", false)) {
    std::cout << "  Global " << args << " 已删除" << std::endl;
    if (args == activeGlobalId_) {
      activeGlobalId_ = "g_default";
      std::cout << "  已切换回默认 Global: g_default" << std::endl;
    }
  } else {
    printError(resp);
  }
}

// ── 任务命令 ──

void CliController::handleTaskCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task create <目标描述>" << std::endl;
    return;
  }
  if (activeGlobalId_.empty()) {
    std::cout << "  [错误] 当前没有活动 Global，请先使用 /global use "
                 "<global_id>"
              << std::endl;
    return;
  }
  if (activeWorkspaceId_.empty()) {
    std::cout << "  [错误] 当前没有活动 Workspace，请先使用 /workspace use "
                 "<workspace_id>"
              << std::endl;
    return;
  }
  if (activeSessionId_.empty()) {
    std::cout << "  [错误] 当前没有活动 Session，请先使用 /session create "
                 "<标题>"
              << std::endl;
    return;
  }

  std::cout << "  [创建任务] " << args << std::endl;
  std::cout << "  Session: " << activeSessionId_
            << "  Global: " << activeGlobalId_
            << "  Workspace: " << activeWorkspaceId_ << std::endl;
  auto resp = client_.createTask(args, activeSessionId_, activeGlobalId_,
                                 activeWorkspaceId_);

  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }

  std::string taskId = resp["data"].value("id", "");
  std::string status = resp["data"].value("status", "");
  std::cout << "  任务 ID: " << taskId << "  状态: " << status << std::endl;

  if (status == "running") {
    std::cout << "  [SSE 监听中... 等待任务完成]\n" << std::endl;
    startStreamListener(taskId);
  }
}

void CliController::handleTaskCancel(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task cancel <task_id>" << std::endl;
    return;
  }
  auto resp = client_.cancelTask(args);
  if (resp.value("success", false)) {
    std::cout << "  任务 " << args << " 已取消" << std::endl;
  } else {
    printError(resp);
  }
}

void CliController::handleTaskList() {
  auto resp = client_.listTasks();
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

void CliController::handleTaskStatus(const std::string &args) {
  auto resp = client_.getTask(args);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto data = resp["data"];
  std::cout << "  ID: " << data.value("id", "") << std::endl;
  std::cout << "  状态: " << data.value("status", "") << std::endl;
  std::cout << "  目标: " << data.value("goal", "") << std::endl;
  std::cout << "  Global: " << data.value("global_id", "") << std::endl;
  std::cout << "  Workspace: " << data.value("workspace_id", "") << std::endl;
}

void CliController::handleTaskHistory(const std::string &args) {
  auto resp = client_.getTaskHistory(args);
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

void CliController::handleTaskActive() {
  auto resp = client_.listActiveTasks();
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

void CliController::handleTaskDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /task delete <task_id>" << std::endl;
    return;
  }
  auto resp = client_.deleteTask(args);
  if (resp.value("success", false)) {
    std::cout << "  任务 " << args << " 已删除" << std::endl;
  } else {
    printError(resp);
  }
}

// ── 工作区命令 ──

void CliController::handleWorkspaceCreate(const std::string &args) {
  auto spacePos = args.find(' ');
  if (spacePos == std::string::npos) {
    std::cout << "  用法: /workspace create <name> <path>" << std::endl;
    return;
  }
  std::string name = args.substr(0, spacePos);
  std::string path = args.substr(spacePos + 1);
  auto resp = client_.createWorkspace(name, path);
  if (resp.value("success", false)) {
    auto data = resp["data"];
    std::cout << "  工作区已创建: " << data.value("id", "?") << " ("
              << data.value("name", "") << ")" << std::endl;
    workspacePath_ = path;
  } else {
    printError(resp);
  }
}

void CliController::handleWorkspaceList() {
  auto resp = client_.listWorkspaces();
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto items = resp["data"]["items"];
  if (items.empty()) {
    std::cout << "  (暂无工作区，请先 /workspace create)" << std::endl;
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

void CliController::handleWorkspaceShow(const std::string &args) {
  std::string id = args.empty() ? activeWorkspaceId_ : args;
  auto resp = client_.getWorkspace(id);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto data = resp["data"];
  std::string marker = (id == activeWorkspaceId_) ? " ← 当前" : "";
  std::cout << "  Workspace: " << data.value("id", "") << marker << std::endl;
  std::cout << "  名称: " << data.value("name", "") << std::endl;
  std::cout << "  路径: " << data.value("path", "") << std::endl;
}

void CliController::handleWorkspaceUse(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /workspace use <workspace_id>" << std::endl;
    std::cout << "  当前: " << activeWorkspaceId_ << std::endl;
    return;
  }
  auto resp = client_.getWorkspace(args);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  if (activeWorkspaceId_ != args) {
    activeSessionId_.clear();
  }
  activeWorkspaceId_ = args;
  workspacePath_ = resp["data"].value("path", "");
  std::cout << "  已切换到工作区: " << activeWorkspaceId_ << std::endl;
  std::cout << "  路径: " << workspacePath_ << std::endl;
  if (activeSessionId_.empty()) {
    std::cout << "  当前没有活动 Session，请使用 /session create <标题>"
              << std::endl;
  }
}

void CliController::handleWorkspaceFiles(const std::string &args) {
  std::string id = args.empty() ? activeWorkspaceId_ : args;
  auto resp = client_.getWorkspaceFileTree(id);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  auto data = resp["data"];
  std::string tree = data.value("tree", data.dump());
  std::cout << "  文件树 (" << data.value("root", id) << "):\n"
            << tree << std::endl;
}

void CliController::handleWorkspaceDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /workspace delete <workspace_id>" << std::endl;
    return;
  }
  auto resp = client_.deleteWorkspace(args);
  if (resp.value("success", false)) {
    std::cout << "  工作区 " << args << " 已删除" << std::endl;
    if (args == activeWorkspaceId_) {
      activeWorkspaceId_.clear();
      activeSessionId_.clear();
      workspacePath_.clear();
      std::cout << "  当前 Workspace 和 Session 已清除" << std::endl;
    }
  } else {
    printError(resp);
  }
}

// ── 对话命令 ──

void CliController::handleSessionCreate(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /session create <标题>" << std::endl;
    return;
  }
  if (activeWorkspaceId_.empty()) {
    std::cout << "  [错误] 当前没有活动 Workspace，请先使用 /workspace use "
                 "<workspace_id>"
              << std::endl;
    return;
  }

  auto resp = client_.createSession(args);
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }

  auto data = resp["data"];
  const std::string sessionId = data.value("id", "");
  if (sessionId.empty()) {
    std::cout << "  [错误] Session 已创建，但后端未返回有效 Session ID"
              << std::endl;
    return;
  }

  auto bindResp =
      client_.updateSession(sessionId, "", "", activeWorkspaceId_);
  if (!bindResp.value("success", false)) {
    std::cout << "  [错误] Session " << sessionId
              << " 已创建，但绑定当前 Workspace 失败" << std::endl;
    printError(bindResp);
    return;
  }

  const std::string boundWorkspaceId =
      bindResp["data"].value("workspace_id", "");
  if (boundWorkspaceId != activeWorkspaceId_) {
    std::cout << "  [错误] Session " << sessionId
              << " 未绑定到当前 Workspace，未激活" << std::endl;
    return;
  }

  activeSessionId_ = sessionId;
  std::cout << "  对话已创建: " << activeSessionId_ << " ("
            << data.value("title", "") << ")" << std::endl;
  std::cout << "  当前活动 Session: " << activeSessionId_ << std::endl;
}

void CliController::handleSessionUpdate(const std::string &args) {
  if (args.empty()) {
    std::cout
        << "  用法: /session update <id> title=xxx alias=yyy workspace=zzz"
        << std::endl;
    return;
  }
  auto spacePos = args.find(' ');
  std::string id;
  std::string kvArgs;
  if (spacePos == std::string::npos) {
    id = args;
  } else {
    id = args.substr(0, spacePos);
    kvArgs = args.substr(spacePos + 1);
  }

  std::string title, alias, workspaceId;
  // Parse key=value pairs
  size_t pos = 0;
  while (pos < kvArgs.size()) {
    size_t eqPos = kvArgs.find('=', pos);
    if (eqPos == std::string::npos)
      break;
    size_t spaceEnd = kvArgs.find(' ', eqPos);
    if (spaceEnd == std::string::npos)
      spaceEnd = kvArgs.size();
    std::string key = kvArgs.substr(pos, eqPos - pos);
    std::string val = kvArgs.substr(eqPos + 1, spaceEnd - eqPos - 1);
    if (key == "title")
      title = val;
    else if (key == "alias")
      alias = val;
    else if (key == "workspace")
      workspaceId = val;
    pos = spaceEnd + 1;
  }

  if (title.empty() && alias.empty() && workspaceId.empty()) {
    std::cout << "  至少指定 title=xxx, alias=xxx 或 workspace=xxx 之一"
              << std::endl;
    return;
  }

  auto resp = client_.updateSession(id, title, alias, workspaceId);
  if (resp.value("success", false)) {
    auto data = resp["data"];
    std::cout << "  对话已更新: " << data.value("id", "?") << " ("
              << data.value("title", "") << ")" << std::endl;
    std::string al = data.value("alias", "");
    if (!al.empty())
      std::cout << "  别名: " << al << std::endl;
  } else {
    printError(resp);
  }
}

void CliController::handleSessionList() {
  auto resp = client_.listSessions();
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
    std::string alias = s.value("alias", "");
    std::string display = alias.empty() ? s.value("title", "?") : alias;
    std::cout << "  " << s.value("id", "?") << "  " << display << "  ["
              << s.value("created_at", "").substr(0, 19) << "]" << std::endl;
  }
}

void CliController::handleSessionDelete(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /session delete <session_id>" << std::endl;
    return;
  }
  auto resp = client_.deleteSession(args);
  if (resp.value("success", false)) {
    std::cout << "  对话 " << args << " 已删除" << std::endl;
    if (args == activeSessionId_) {
      activeSessionId_.clear();
      std::cout << "  当前活动 Session 已清除" << std::endl;
    }
  } else {
    printError(resp);
  }
}

// ── 权限命令 ──

void CliController::handlePermList(const std::string &taskId) {
  auto resp = client_.listPermissions(taskId);
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

void CliController::handlePermApprove(const std::string &args) {
  // 无参 = 自动审批第一个 pending
  if (args.empty()) {
    auto resp = client_.approveFirstPending();
    if (resp.value("success", false)) {
      auto data = resp["data"];
      std::cout << "  权限 " << data.value("id", "?") << " 已批准 (自动)"
                << std::endl;
    } else {
      printError(resp);
    }
    return;
  }
  auto resp = client_.approvePermission(args);
  if (resp.value("success", false)) {
    std::cout << "  权限 " << args << " 已批准" << std::endl;
  } else {
    printError(resp);
  }
}

void CliController::handlePermReject(const std::string &args) {
  if (args.empty()) {
    std::cout << "  用法: /perm reject <permission_id>" << std::endl;
    return;
  }
  auto resp = client_.rejectPermission(args);
  if (resp.value("success", false)) {
    std::cout << "  权限 " << args << " 已拒绝" << std::endl;
  } else {
    printError(resp);
  }
}

// ── 查询命令 ──

void CliController::handleTools() {
  auto resp = client_.listTools();
  if (!resp.value("success", false)) {
    printError(resp);
  }
  json tools;
  if (resp.contains("data") && resp["data"].contains("items")) {
    tools = resp["data"]["items"];
  } else if (resp.contains("data") && resp["data"].contains("tools")) {
    tools = resp["data"]["tools"];
  } else if (resp.contains("data") && resp["data"].is_array()) {
    tools = resp["data"];
  } else if (resp.contains("tools")) {
    tools = resp["tools"];
  } else {
    std::cout << "  (无法解析工具列表) 原始响应: " << resp.dump().substr(0, 200)
              << std::endl;
    return;
  }
  if (tools.empty()) {
    std::cout << "  (暂无工具)" << std::endl;
    return;
  }
  for (const auto &t : tools) {
    std::cout << "  " << t.value("name", "?") << " - "
              << t.value("description", "").substr(0, 60) << std::endl;
  }
}

void CliController::handleExpertsList() {
  auto resp = client_.listExperts();
  if (!resp.value("success", false)) {
    printError(resp);
    return;
  }
  json experts;
  if (resp.contains("data") && resp["data"].contains("items")) {
    experts = resp["data"]["items"];
  } else if (resp.contains("data") && resp["data"].contains("experts")) {
    experts = resp["data"]["experts"];
  } else if (resp.contains("data") && resp["data"].is_array()) {
    experts = resp["data"];
  }
  if (experts.empty()) {
    std::cout << "  (暂无 Expert)" << std::endl;
    return;
  }
  for (const auto &e : experts) {
    std::cout << "  " << e.value("name", "?")
              << (e.value("is_entry", false) ? " [入口]" : "") << std::endl;
  }
}

void CliController::handleHealth() {
  bool ok = client_.healthCheck();
  std::cout << "  服务器状态: " << (ok ? "正常" : "异常") << std::endl;
}

void CliController::handleVerbose(const std::string &args) {
  if (args == "on") {
    verbose_ = true;
    printer_.setVerbose(true);
    std::cout << "  调试日志: 开启" << std::endl;
  } else {
    verbose_ = false;
    printer_.setVerbose(false);
    std::cout << "  调试日志: 关闭" << std::endl;
  }
}

// ── SSE 流监听 ──

void CliController::startStreamListener(const std::string &taskId) {
  stopStreamListener();
  streamCancelFlag_.store(false);
  streamThread_ = std::thread([this, taskId]() {
    client_.streamEvents(
        taskId, [this](const json &event) { printer_.onEvent(event); },
        [this]() {
          if (!streamCancelFlag_.load()) {
            std::cout << "\n  [SSE 流已结束]\n> " << std::flush;
          }
        },
        streamCancelFlag_);
  });
}

void CliController::stopStreamListener() {
  streamCancelFlag_.store(true);
  if (streamThread_.joinable()) {
    streamThread_.join();
  }
}

#pragma once

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class ApiClient {
public:
  ApiClient(const std::string &host = "localhost", int port = 8080);

  bool healthCheck();
  std::string baseUrl() const;

  // ── 任务操作 ──
  json createTask(const std::string &input, const std::string &sessionId,
                  const std::string &globalId,
                  const std::string &workspaceId);
  json cancelTask(const std::string &taskId);
  json getTask(const std::string &taskId);
  json listTasks(int page = 1, int pageSize = 20);
  json listActiveTasks();
  json getTaskHistory(const std::string &taskId);

  // ── SSE 实时事件流 ──
  void streamEvents(const std::string &taskId,
                    std::function<void(const json &event)> onEvent,
                    std::function<void()> onStreamEnd,
                    std::atomic_bool &cancelFlag);

  // ── 查询类 ──
  json listTools(const std::string &group = "");
  json getExpertGraph();
  json listExperts();

  // ── Workspace 管理 ──
  json createWorkspace(const std::string &name, const std::string &path);
  json listWorkspaces();

  // ── Session 管理 ──
  json createSession(const std::string &title);
  json listSessions();
  json updateSession(const std::string &id, const std::string &title,
                     const std::string &alias, const std::string &workspaceId);
  json deleteSession(const std::string &id);

  // ── Global 管理 ★ v2 ──
  json createGlobal(const std::string &name,
                    const std::string &description = "",
                    const std::string &workspace_id = "");
  json listGlobals();
  json getGlobal(const std::string &id);
  json getGlobalContext(const std::string &id);
  json deleteGlobal(const std::string &id);

  // ── Workspace 扩展 ★ v2 ──
  json getWorkspace(const std::string &id);
  json getWorkspaceFileTree(const std::string &id);
  json deleteWorkspace(const std::string &id);

  // ── Task 扩展 ★ v2 ──
  json deleteTask(const std::string &id);

  // ── 权限管理 ★ v2 ──
  json listPermissions(const std::string &taskId = "");
  json approvePermission(const std::string &id);
  json rejectPermission(const std::string &id);
  json approveFirstPending();

private:
  std::string host_;
  int port_;
  std::string doGet(const std::string &path);
  std::string doPost(const std::string &path, const std::string &body);
  std::string doPut(const std::string &path, const std::string &body);
  std::string doDelete(const std::string &path);
};

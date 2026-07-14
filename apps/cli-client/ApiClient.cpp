#include "ApiClient.h"

#include "httplib.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

ApiClient::ApiClient(const std::string &host, int port)
    : host_(host), port_(port) {}

std::string ApiClient::baseUrl() const {
  return host_ + ":" + std::to_string(port_);
}

bool ApiClient::healthCheck() {
  auto res = doGet("/api/v1/health");
  if (res.empty()) {
    res = doGet("/health");
  }
  if (res.empty()) {
    std::cerr << "[DEBUG] healthCheck: no response" << std::endl;
    return false;
  }
  try {
    auto j = json::parse(res);
    bool ok = j.value("success", false) || j.value("status", "") == "ok";
    std::cerr << "[DEBUG] healthCheck: " << (ok ? "OK" : "FAIL")
              << " body=" << res.substr(0, 200) << std::endl;
    return ok;
  } catch (const std::exception &e) {
    if (res.find("OK") != std::string::npos ||
        res.find("ok") != std::string::npos) {
      std::cerr << "[DEBUG] healthCheck: plaintext OK" << std::endl;
      return true;
    }
    std::cerr << "[DEBUG] healthCheck parse error: " << e.what()
              << " body=" << res.substr(0, 200) << std::endl;
    return false;
  }
}

std::string ApiClient::doGet(const std::string &path) {
  std::cerr << "[DEBUG] GET " << baseUrl() << path << std::endl;
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Get(path);
  if (!res) {
    std::cerr << "[DEBUG] GET " << path << " FAILED: no response" << std::endl;
    return "";
  }
  std::cerr << "[DEBUG] GET " << path << " status=" << res->status
            << " body_len=" << res->body.size() << std::endl;
  return res->body;
}

std::string ApiClient::doPost(const std::string &path,
                              const std::string &body) {
  std::cerr << "[DEBUG] POST " << baseUrl() << path
            << " body=" << body.substr(0, 200) << std::endl;
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Post(path, body, "application/json");
  if (!res) {
    std::cerr << "[DEBUG] POST " << path << " FAILED: no response" << std::endl;
    return "";
  }
  std::cerr << "[DEBUG] POST " << path << " status=" << res->status
            << " body=" << res->body.substr(0, 300) << std::endl;
  return res->body;
}

std::string ApiClient::doPut(const std::string &path, const std::string &body) {
  std::cerr << "[DEBUG] PUT " << baseUrl() << path
            << " body=" << body.substr(0, 200) << std::endl;
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Put(path, body, "application/json");
  if (!res) {
    std::cerr << "[DEBUG] PUT " << path << " FAILED: no response" << std::endl;
    return "";
  }
  std::cerr << "[DEBUG] PUT " << path << " status=" << res->status
            << " body=" << res->body.substr(0, 300) << std::endl;
  return res->body;
}

std::string ApiClient::doDelete(const std::string &path) {
  std::cerr << "[DEBUG] DELETE " << baseUrl() << path << std::endl;
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(3);
  cli.set_read_timeout(10);
  auto res = cli.Delete(path);
  if (!res) {
    std::cerr << "[DEBUG] DELETE " << path << " FAILED: no response"
              << std::endl;
    return "";
  }
  std::cerr << "[DEBUG] DELETE " << path << " status=" << res->status
            << std::endl;
  return res->body;
}

static json wrapError(const std::string &msg) {
  json err;
  err["success"] = false;
  err["error"] = {{"message", msg}};
  return err;
}

static json parseOrError(const std::string &resp, const std::string &op) {
  if (resp.empty())
    return wrapError(op + ": 无响应");
  try {
    return json::parse(resp);
  } catch (const std::exception &e) {
    return wrapError(op + ": 解析失败 - " + std::string(e.what()));
  }
}

json ApiClient::createTask(const std::string &goal,
                           const std::string &sessionId,
                           const std::string &globalId,
                           const std::string &workspaceId) {
  json body;
  body["session_id"] = sessionId;
  body["global_id"] = globalId;
  body["workspace_id"] = workspaceId;
  body["input"] = goal;
  return parseOrError(doPost("/api/v1/tasks", body.dump()), "createTask");
}

json ApiClient::cancelTask(const std::string &taskId) {
  return parseOrError(doPost("/api/v1/tasks/" + taskId + "/cancel", "{}"),
                      "cancelTask");
}

json ApiClient::getTask(const std::string &taskId) {
  return parseOrError(doGet("/api/v1/tasks/" + taskId), "getTask");
}

json ApiClient::listTasks(int page, int pageSize) {
  std::string path = "/api/v1/tasks?page=" + std::to_string(page) +
                     "&page_size=" + std::to_string(pageSize);
  std::string resp = doGet(path);
  if (resp.empty())
    return {{"success", true}, {"data", {{"items", json::array()}}}};
  return parseOrError(resp, "listTasks");
}

json ApiClient::listActiveTasks() {
  std::string resp = doGet("/api/v1/tasks/active");
  if (resp.empty())
    return {{"success", true}, {"data", {{"items", json::array()}}}};
  return parseOrError(resp, "listActiveTasks");
}

json ApiClient::getTaskHistory(const std::string &taskId) {
  std::string resp = doGet("/api/v1/tasks/" + taskId + "/events/history");
  if (resp.empty())
    return {{"success", true}, {"data", {{"items", json::array()}}}};
  return parseOrError(resp, "getTaskHistory");
}

void ApiClient::streamEvents(const std::string &taskId,
                             std::function<void(const json &event)> onEvent,
                             std::function<void()> onStreamEnd,
                             std::atomic_bool &cancelFlag) {
  httplib::Client cli(baseUrl());
  cli.set_connection_timeout(10);
  cli.set_read_timeout(300, 0);
  cli.set_write_timeout(300, 0);
  cli.set_keep_alive(true);

  std::string path = "/api/v1/tasks/" + taskId + "/events";

  std::string lineBuffer;
  std::string eventData;
  int eventCount = 0;
  int rawChunkCount = 0;
  int totalBytes = 0;

  auto contentReceiver = [&](const char *data, size_t len) -> bool {
    if (cancelFlag.load())
      return false;
    ++rawChunkCount;
    totalBytes += static_cast<int>(len);
    lineBuffer.append(data, len);
    static int sseDebugCount = 0;
    if (++sseDebugCount <= 3) {
      std::cerr << "[SSE DEBUG] contentReceiver called #" << sseDebugCount
                << " len=" << len << std::endl;
    }
    size_t pos;
    int linesProcessed = 0;
    while ((pos = lineBuffer.find('\n')) != std::string::npos) {
      std::string line = lineBuffer.substr(0, pos);
      lineBuffer.erase(0, pos + 1);
      ++linesProcessed;
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      if (line.empty()) {
        if (!eventData.empty()) {
          try {
            auto event = json::parse(eventData);
            eventCount++;
            try {
              onEvent(event);
            } catch (const std::exception &e) {
              std::cerr << "[SSE TRACE] CLI callback err: " << e.what()
                        << std::endl;
            }
          } catch (const std::exception &) {
          }
          eventData.clear();
        }
      } else if (line[0] == ':') {
        // SSE comment, ignore
      } else if (line.rfind("data: ", 0) == 0) {
        eventData = line.substr(6);
      } else if (!line.empty()) {
        // "event:" lines fall here, silently skip
      }
    }
    return true;
  };

  auto res = cli.Get(path, contentReceiver);
  if (!res) {
    std::cerr << "[DEBUG] SSE connection failed (no HTTP response)"
              << std::endl;
  }
  onStreamEnd();
}

json ApiClient::listTools(const std::string &group) {
  std::string path = "/api/v1/tools";
  if (!group.empty())
    path += "?group=" + group;
  return parseOrError(doGet(path), "listTools");
}

json ApiClient::getExpertGraph() {
  return parseOrError(doGet("/api/v1/experts/graph"), "getExpertGraph");
}

json ApiClient::listExperts() {
  return parseOrError(doGet("/api/v1/experts"), "listExperts");
}

json ApiClient::createWorkspace(const std::string &name,
                                const std::string &path) {
  json body;
  body["name"] = name;
  body["path"] = path;
  return parseOrError(doPost("/api/v1/workspaces", body.dump()),
                      "createWorkspace");
}

json ApiClient::listWorkspaces() {
  return parseOrError(doGet("/api/v1/workspaces"), "listWorkspaces");
}

json ApiClient::createSession(const std::string &title) {
  json body;
  body["title"] = title;
  return parseOrError(doPost("/api/v1/sessions", body.dump()), "createSession");
}

json ApiClient::listSessions() {
  return parseOrError(doGet("/api/v1/sessions"), "listSessions");
}

json ApiClient::updateSession(const std::string &id, const std::string &title,
                              const std::string &alias,
                              const std::string &workspaceId) {
  json body;
  if (!title.empty())
    body["title"] = title;
  if (!alias.empty())
    body["alias"] = alias;
  if (!workspaceId.empty())
    body["workspace_id"] = workspaceId;
  return parseOrError(doPut("/api/v1/sessions/" + id, body.dump()),
                      "updateSession");
}

json ApiClient::deleteSession(const std::string &id) {
  return parseOrError(doDelete("/api/v1/sessions/" + id), "deleteSession");
}

// ── Global 管理 ★ v2 ──

json ApiClient::createGlobal(const std::string &name,
                             const std::string &description,
                             const std::string &workspace_id) {
  json body;
  body["name"] = name;
  if (!description.empty())
    body["description"] = description;
  if (!workspace_id.empty())
    body["workspace_id"] = workspace_id;
  return parseOrError(doPost("/api/v1/globals", body.dump()), "createGlobal");
}

json ApiClient::listGlobals() {
  return parseOrError(doGet("/api/v1/globals"), "listGlobals");
}

json ApiClient::getGlobal(const std::string &id) {
  return parseOrError(doGet("/api/v1/globals/" + id), "getGlobal");
}

json ApiClient::getGlobalContext(const std::string &id) {
  return parseOrError(doGet("/api/v1/globals/" + id + "/context"),
                      "getGlobalContext");
}

json ApiClient::deleteGlobal(const std::string &id) {
  return parseOrError(doDelete("/api/v1/globals/" + id), "deleteGlobal");
}

// ── Workspace 扩展 ★ v2 ──

json ApiClient::getWorkspace(const std::string &id) {
  return parseOrError(doGet("/api/v1/workspaces/" + id), "getWorkspace");
}

json ApiClient::getWorkspaceFileTree(const std::string &id) {
  return parseOrError(doGet("/api/v1/workspaces/" + id + "/files/tree"),
                      "getWorkspaceFileTree");
}

json ApiClient::deleteWorkspace(const std::string &id) {
  return parseOrError(doDelete("/api/v1/workspaces/" + id), "deleteWorkspace");
}

// ── Task 扩展 ★ v2 ──

json ApiClient::deleteTask(const std::string &id) {
  return parseOrError(doDelete("/api/v1/tasks/" + id), "deleteTask");
}

// ── 权限管理 ★ v2 ──

json ApiClient::listPermissions() {
  return parseOrError(doGet("/api/v1/permissions"), "listPermissions");
}

json ApiClient::approvePermission(const std::string &id) {
  json body;
  body["action"] = "approve";
  return parseOrError(
      doPost("/api/v1/permissions/" + id + "/approve", body.dump()),
      "approvePermission");
}

json ApiClient::rejectPermission(const std::string &id) {
  json body;
  body["action"] = "reject";
  return parseOrError(
      doPost("/api/v1/permissions/" + id + "/reject", body.dump()),
      "rejectPermission");
}

json ApiClient::approveFirstPending() {
  return parseOrError(doPost("/api/v1/permissions/approve-first", "{}"),
                      "approveFirstPending");
}

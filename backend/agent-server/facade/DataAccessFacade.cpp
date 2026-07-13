#include "facade/DataAccessFacade.h"
#include "infrastructure/storage/SqliteConnection.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace codepilot {

// ============================================================
// 单例获取
// ============================================================
DataAccessFacade &DataAccessFacade::getInstance() {
  static DataAccessFacade instance;
  return instance;
}

// ============================================================
// 析构
// ============================================================
DataAccessFacade::~DataAccessFacade() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

// ============================================================
// ISO 8601 时间戳（集中实现，消除重复）
// ============================================================
std::string DataAccessFacade::iso8601Now() {
  auto t =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm *tm = std::gmtime(&t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return std::string(buf);
}

// ============================================================
// ID 生成
// ============================================================
std::string DataAccessFacade::generateId(const std::string &prefix) const {
  static std::atomic<uint64_t> counter{0};
  const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
  return prefix + "_" + std::to_string(ts) + "_" + std::to_string(++counter);
}

// ============================================================
// 安全调用包装
// ============================================================
template <typename T>
T DataAccessFacade::safeCall(const std::string &operation,
                             std::function<T()> fn, T fallback) const {
  try {
    return fn();
  } catch (const std::exception &) {
    return fallback;
  }
}

// ============================================================
// init — 幂等初始化
// ============================================================
void DataAccessFacade::init(const std::string &dbPath) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return;
  }

  if (openSqliteConnection(dbPath.c_str(), &db_) != SQLITE_OK) {
    const std::string error = db_ ? sqlite3_errmsg(db_) : "sqlite_open_failed";
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw std::runtime_error(error);
  }

  configureSqliteDatabase(db_);

  try {
    createAllTables();
  } catch (...) {
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }

  initialized_ = true;
}

// ============================================================
// 建表 — 所有 Repository 表 + system_health + task_contexts
// ============================================================
void DataAccessFacade::createAllTables() {
  GlobalRepository(db_).initTable();
  GlobalRepository(db_).initContextTable();
  SessionRepository(db_).initTable();
  WorkspaceRepository(db_).initTable();
  TaskRepository(db_).initTable();
  EventRepository(db_).initTable();
  ToolCallRepository(db_).initTable();
  PermissionRepository(db_).initTable();
  FileChangeRepository(db_).initTable();
  LogRepository(db_).initTable();

  // task_contexts 表（用于主循环中断恢复）
  sqlite3_exec(db_,
               "CREATE TABLE IF NOT EXISTS task_contexts ("
               "  task_id TEXT PRIMARY KEY,"
               "  context_json TEXT NOT NULL,"
               "  updated_at TEXT NOT NULL"
               ");",
               nullptr, nullptr, nullptr);

  // system_health 表（由 AppBootstrap 迁移至此，消除双连接）
  sqlite3_exec(db_,
               "CREATE TABLE IF NOT EXISTS system_health ("
               "    id INTEGER PRIMARY KEY CHECK (id = 1),"
               "    service TEXT NOT NULL,"
               "    checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
               ");",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_,
               "INSERT INTO system_health (id, service, checked_at) "
               "VALUES (1, 'codepilot-agent-server', CURRENT_TIMESTAMP) "
               "ON CONFLICT(id) DO UPDATE SET checked_at = CURRENT_TIMESTAMP;",
               nullptr, nullptr, nullptr);
}

// ============================================================
// 健康检查
// ============================================================
bool DataAccessFacade::isDatabaseConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  return db_ != nullptr;
}

// ============================================================
// Session 组
// ============================================================
SessionRecord DataAccessFacade::createSession(const std::string &title) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  return SessionRepository(db_).createSession(generateId("session"), title, now,
                                              now);
}

std::optional<SessionRecord>
DataAccessFacade::getSession(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return SessionRepository(db_).findById(id);
}

std::vector<SessionRecord> DataAccessFacade::listSessions() {
  std::lock_guard<std::mutex> lock(mutex_);
  return SessionRepository(db_).listAll();
}

bool DataAccessFacade::deleteSession(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "deleteSession", [&]() { return SessionRepository(db_).deleteById(id); },
      false);
}

// ============================================================
// Global 组
// ============================================================
GlobalRecord DataAccessFacade::createGlobal(const std::string &name,
                                            const std::string &description) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  return GlobalRepository(db_).createGlobal(generateId("g"), name, description,
                                            now, now);
}

std::optional<GlobalRecord> DataAccessFacade::getGlobal(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return GlobalRepository(db_).findById(id);
}

std::vector<GlobalRecord> DataAccessFacade::listGlobals() {
  std::lock_guard<std::mutex> lock(mutex_);
  return GlobalRepository(db_).listAll();
}

bool DataAccessFacade::deleteGlobal(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "deleteGlobal", [&]() { return GlobalRepository(db_).deleteById(id); },
      false);
}

std::string DataAccessFacade::ensureDefaultGlobal() {
  std::lock_guard<std::mutex> lock(mutex_);
  GlobalRepository repo(db_);
  if (repo.count() > 0) {
    auto all = repo.listAll();
    if (!all.empty()) {
      return all.front().id;
    }
  }
  // Create default global
  const std::string now = iso8601Now();
  auto rec = repo.createGlobal("g_default", "\u9ed8\u8ba4\u5de5\u4f5c\u533a",
                               "", now, now);
  return rec.id;
}

void DataAccessFacade::saveGlobalContext(const std::string &globalId,
                                         const std::string &sourceTaskId,
                                         const std::string &type,
                                         const std::string &content) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  GlobalRepository(db_).saveContext(globalId, sourceTaskId, type, content, now);
}

std::vector<GlobalContextRecord>
DataAccessFacade::getGlobalContext(const std::string &globalId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return GlobalRepository(db_).getContextByGlobalId(globalId);
}

// ============================================================
// Task 组
// ============================================================
TaskRecord DataAccessFacade::createTask(const std::string &globalId,
                                        const std::string &workspaceId,
                                        const std::string &goal) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  return TaskRepository(db_).createTask(generateId("task"), globalId,
                                        workspaceId, goal, now, now);
}

std::optional<TaskRecord> DataAccessFacade::getTask(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return TaskRepository(db_).findById(id);
}

bool DataAccessFacade::updateTaskStatus(const std::string &id,
                                        const std::string &status,
                                        const std::string &plan,
                                        const std::string &currentStep) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "updateTaskStatus",
      [&]() {
        const std::string now = iso8601Now();
        TaskRepository(db_).updateExecution(id, status, plan, currentStep, now);
        return true;
      },
      false);
}

std::vector<TaskRecord>
DataAccessFacade::listTasksBySession(const std::string &sessionId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<std::vector<TaskRecord>>(
      "listTasksBySession",
      [&]() { return TaskRepository(db_).findBySessionId(sessionId); },
      std::vector<TaskRecord>{});
}

std::vector<TaskRecord>
DataAccessFacade::listTasksByGlobal(const std::string &globalId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<std::vector<TaskRecord>>(
      "listTasksByGlobal",
      [&]() { return TaskRepository(db_).findBySessionId(globalId); },
      std::vector<TaskRecord>{});
}

std::vector<TaskRecord> DataAccessFacade::listRecentTasks(int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  return TaskRepository(db_).listRecent(limit);
}

// ============================================================
// Workspace 组
// ============================================================
WorkspaceRecord DataAccessFacade::createWorkspace(const std::string &name,
                                                  const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  return safeCall<WorkspaceRecord>(
      "createWorkspace",
      [&]() {
        return WorkspaceRepository(db_).create(generateId("ws"), name, path,
                                               now);
      },
      WorkspaceRecord{});
}

std::optional<WorkspaceRecord>
DataAccessFacade::getWorkspace(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return WorkspaceRepository(db_).findById(id);
}

std::vector<WorkspaceRecord> DataAccessFacade::listWorkspaces() {
  std::lock_guard<std::mutex> lock(mutex_);
  return WorkspaceRepository(db_).listAll();
}

// ============================================================
// 执行日志组
// ============================================================
sqlite3_int64 DataAccessFacade::appendLog(const std::string &taskId,
                                          const std::string &type,
                                          const std::string &content) {
  std::lock_guard<std::mutex> lock(mutex_);
  return LogRepository(db_).createLog(taskId, type, content);
}

std::vector<LogRecord>
DataAccessFacade::getLogsByTaskId(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return LogRepository(db_).findByTaskId(taskId);
}

// ============================================================
// 事件日志组
// ============================================================
EventRecord DataAccessFacade::saveEvent(const std::string &id,
                                        const std::string &taskId,
                                        const std::string &type,
                                        const std::string &content,
                                        const std::string &metadata) {
  std::lock_guard<std::mutex> lock(mutex_);
  return EventRepository(db_).create(id, taskId, type, content, metadata);
}

std::vector<EventRecord>
DataAccessFacade::getEventsByTaskId(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return EventRepository(db_).findByTaskId(taskId);
}

std::vector<EventRecord>
DataAccessFacade::getEventsByTaskIdAndType(const std::string &taskId,
                                           const std::string &type) {
  std::lock_guard<std::mutex> lock(mutex_);
  return EventRepository(db_).findByTaskIdAndType(taskId, type);
}

// ============================================================
// 工具调用组
// ============================================================
ToolCallRecord
DataAccessFacade::saveToolCall(const std::string &id, const std::string &taskId,
                               const std::string &toolName,
                               const std::string &arguments, bool success,
                               const std::string &result, int exitCode) {
  std::lock_guard<std::mutex> lock(mutex_);
  return ToolCallRepository(db_).create(id, taskId, toolName, arguments,
                                        success, result, exitCode);
}

std::vector<ToolCallRecord>
DataAccessFacade::getToolCallsByTaskId(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return ToolCallRepository(db_).findByTaskId(taskId);
}

// ============================================================
// 权限请求组
// ============================================================
PermissionRequest DataAccessFacade::createPermissionRequest(
    const std::string &taskId, const std::string &toolName,
    const std::string &riskLevel, const std::string &action,
    const std::string &reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string now = iso8601Now();
  return safeCall<PermissionRequest>(
      "createPermissionRequest",
      [&]() {
        return PermissionRepository(db_).create(generateId("perm"), taskId,
                                                toolName, riskLevel, action,
                                                reason, now);
      },
      PermissionRequest{});
}

std::optional<PermissionRequest>
DataAccessFacade::getPermissionRequest(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return PermissionRepository(db_).findById(id);
}

bool DataAccessFacade::resolvePermission(const std::string &id, bool approved) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "resolvePermission",
      [&]() {
        PermissionRepository(db_).updateStatus(id, approved ? "approved"
                                                            : "rejected");
        return true;
      },
      false);
}

std::vector<PermissionRequest>
DataAccessFacade::listPendingPermissions(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto all = PermissionRepository(db_).findPending();
  if (taskId.empty()) {
    return all;
  }
  std::vector<PermissionRequest> filtered;
  for (const auto &req : all) {
    if (req.task_id == taskId) {
      filtered.push_back(req);
    }
  }
  return filtered;
}

// ============================================================
// 文件变更组
// ============================================================
FileChangeRecord DataAccessFacade::recordFileChange(
    const std::string &id, const std::string &taskId,
    const std::string &filePath, const std::string &changeType,
    const std::string &diff) {
  std::lock_guard<std::mutex> lock(mutex_);
  return FileChangeRepository(db_).create(id, taskId, filePath, changeType,
                                          diff);
}

std::vector<FileChangeRecord>
DataAccessFacade::getFileChangesByTaskId(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return FileChangeRepository(db_).findByTaskId(taskId);
}

// ============================================================
// 聚合查询组
// ============================================================
ReplayRecord DataAccessFacade::getReplayByTaskId(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return ReplayRepository(db_).getReplayByTaskId(taskId);
}

// ============================================================
// 级联删除
// ============================================================
bool DataAccessFacade::deleteTaskCascade(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "deleteTaskCascade",
      [&]() {
        auto exec = [&](const char *sql) {
          sqlite3_stmt *stmt = nullptr;
          if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_));
          }
          sqlite3_bind_text(stmt, 1, taskId.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_step(stmt);
          sqlite3_finalize(stmt);
        };
        exec("DELETE FROM execution_logs WHERE task_id = ?;");
        exec("DELETE FROM file_changes WHERE task_id = ?;");
        exec("DELETE FROM permission_requests WHERE task_id = ?;");
        exec("DELETE FROM tool_calls WHERE task_id = ?;");
        exec("DELETE FROM task_events WHERE task_id = ?;");
        exec("DELETE FROM task_contexts WHERE task_id = ?;");
        exec("DELETE FROM tasks WHERE id = ?;");
        return true;
      },
      false);
}

// ============================================================
// 任务上下文持久化（支持主循环中断恢复）
// ============================================================
bool DataAccessFacade::saveTaskContext(const std::string &taskId,
                                       const std::string &contextJson) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<bool>(
      "saveTaskContext",
      [&]() {
        const std::string now = iso8601Now();
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "INSERT OR REPLACE INTO task_contexts (task_id, "
                          "context_json, updated_at) "
                          "VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
          throw std::runtime_error(sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, taskId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, contextJson.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
      },
      false);
}

std::string DataAccessFacade::getTaskContext(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return safeCall<std::string>(
      "getTaskContext",
      [&]() {
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "SELECT context_json FROM task_contexts WHERE task_id = ?;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
          throw std::runtime_error(sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, taskId.c_str(), -1, SQLITE_TRANSIENT);
        std::string result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
          const char *text =
              reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
          if (text) {
            result = text;
          }
        }
        sqlite3_finalize(stmt);
        return result;
      },
      std::string());
}

} // namespace codepilot
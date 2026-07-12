#pragma once

#include "infrastructure/storage/repositories/EventRepository.h"
#include "infrastructure/storage/repositories/FileChangeRepository.h"
#include "infrastructure/storage/repositories/LogRepository.h"
#include "infrastructure/storage/repositories/PermissionRepository.h"
#include "infrastructure/storage/repositories/ReplayRepository.h"
#include "infrastructure/storage/repositories/SessionRepository.h"
#include "infrastructure/storage/repositories/TaskRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"
#include "infrastructure/storage/repositories/WorkspaceRepository.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// DataAccessFacade - 存储系统统一入口（单例门面 Facade）
//
// 设计目标（参照 application/ToolSystem.h）：
//   一行 init() → 所有持久化原子操作 → 调用方无需理解内部 Repository 细节
//
// 内部封装 8 个 Repository：
//   SessionRepository, TaskRepository, LogRepository, EventRepository,
//   ToolCallRepository, PermissionRepository, FileChangeRepository,
//   ReplayRepository
//
// 线程安全（使用 mutex 保护所有 db 操作）
// ============================================================
class DataAccessFacade {
public:
  static DataAccessFacade &getInstance();

  // ============================================================
  // 初始化（幂等，重复调用不生效）
  // ============================================================
  void init(const std::string &dbPath);
  bool isInitialized() const { return initialized_; }

  // ============================================================
  // Session 组
  // ============================================================
  SessionRecord createSession(const std::string &title);
  std::optional<SessionRecord> getSession(const std::string &id);
  std::vector<SessionRecord> listSessions();
  bool deleteSession(const std::string &id);

  // ============================================================
  // Task 组
  // ============================================================
  TaskRecord createTask(const std::string &sessionId,
                        const std::string &workspaceId,
                        const std::string &goal);
  std::optional<TaskRecord> getTask(const std::string &id);
  bool updateTaskStatus(const std::string &id, const std::string &status,
                        const std::string &plan,
                        const std::string &currentStep);
  std::vector<TaskRecord> listTasksBySession(const std::string &sessionId);
  std::vector<TaskRecord> listRecentTasks(int limit = 20);

  // ============================================================
  // Workspace 组
  // ============================================================
  WorkspaceRecord createWorkspace(const std::string &name,
                                  const std::string &path);
  std::optional<WorkspaceRecord> getWorkspace(const std::string &id);
  std::vector<WorkspaceRecord> listWorkspaces();

  // ============================================================
  // 执行日志组
  // ============================================================
  sqlite3_int64 appendLog(const std::string &taskId, const std::string &type,
                          const std::string &content);
  std::vector<LogRecord> getLogsByTaskId(const std::string &taskId);

  // ============================================================
  // 事件日志组
  // ============================================================
  EventRecord saveEvent(const std::string &id, const std::string &taskId,
                        const std::string &type, const std::string &content,
                        const std::string &metadata);
  std::vector<EventRecord> getEventsByTaskId(const std::string &taskId);
  std::vector<EventRecord> getEventsByTaskIdAndType(const std::string &taskId,
                                                    const std::string &type);

  // ============================================================
  // 工具调用组
  // ============================================================
  ToolCallRecord saveToolCall(const std::string &id, const std::string &taskId,
                              const std::string &toolName,
                              const std::string &arguments, bool success,
                              const std::string &result, int exitCode);
  std::vector<ToolCallRecord> getToolCallsByTaskId(const std::string &taskId);

  // ============================================================
  // 权限请求组
  // ============================================================
  PermissionRequest createPermissionRequest(const std::string &taskId,
                                            const std::string &toolName,
                                            const std::string &riskLevel,
                                            const std::string &action,
                                            const std::string &reason);
  std::optional<PermissionRequest> getPermissionRequest(const std::string &id);
  bool resolvePermission(const std::string &id, bool approved);
  std::vector<PermissionRequest>
  listPendingPermissions(const std::string &taskId = "");

  // ============================================================
  // 文件变更组
  // ============================================================
  FileChangeRecord recordFileChange(const std::string &id,
                                    const std::string &taskId,
                                    const std::string &filePath,
                                    const std::string &changeType,
                                    const std::string &diff);
  std::vector<FileChangeRecord>
  getFileChangesByTaskId(const std::string &taskId);

  // ============================================================
  // 聚合查询组
  // ============================================================
  ReplayRecord getReplayByTaskId(const std::string &taskId);

  // ============================================================
  // 工具组
  // ============================================================
  bool isDatabaseConnected();
  bool deleteTaskCascade(const std::string &taskId);

  // ============================================================
  // 新增：任务上下文持久化（支持主循环中断恢复）
  // ============================================================
  bool saveTaskContext(const std::string &taskId,
                       const std::string &contextJson);
  std::string getTaskContext(const std::string &taskId);

private:
  DataAccessFacade() = default;
  ~DataAccessFacade();
  DataAccessFacade(const DataAccessFacade &) = delete;
  DataAccessFacade &operator=(const DataAccessFacade &) = delete;

  static std::string iso8601Now();

  template <typename T>
  T safeCall(const std::string &operation, std::function<T()> fn,
             T fallback) const;

  void createAllTables();

  std::string generateId(const std::string &prefix) const;

  mutable std::mutex mutex_;
  sqlite3 *db_ = nullptr;
  bool initialized_ = false;
};

} // namespace codepilot
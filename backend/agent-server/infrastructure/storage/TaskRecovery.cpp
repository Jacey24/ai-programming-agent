#include "infrastructure/storage/TaskRecovery.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace codepilot {
namespace {

constexpr const char *kRecoverableStatuses =
    "('created','planning','pending','running','paused','waiting_permission')";

struct StaleTask {
  std::string id;
  std::string status;
};

enum class RecoveryEventIdState { Available, MatchingRecovery, Conflict };

void execute(sqlite3 *db, const char *sql, const char *operation) {
  char *error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(std::string(operation) + ": " + message);
  }
}

class Statement {
public:
  Statement(sqlite3 *db, const std::string &sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(db));
    }
  }

  ~Statement() { sqlite3_finalize(stmt_); }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  sqlite3_stmt *get() const { return stmt_; }
  void reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }

private:
  sqlite3 *db_;
  sqlite3_stmt *stmt_{nullptr};
};

void bindText(sqlite3 *db, sqlite3_stmt *stmt, int index,
              const std::string &value) {
  if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
}

void stepDone(sqlite3 *db, sqlite3_stmt *stmt, const char *operation) {
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    throw std::runtime_error(std::string(operation) + ": " +
                             sqlite3_errmsg(db));
  }
}

std::vector<StaleTask> findStaleTasks(sqlite3 *db) {
  Statement query(db, std::string("SELECT id, status FROM tasks WHERE status IN ") +
                          kRecoverableStatuses + " ORDER BY id;");
  std::vector<StaleTask> tasks;
  int result = SQLITE_ROW;
  while ((result = sqlite3_step(query.get())) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 0));
    const auto *status =
        reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 1));
    tasks.push_back({id ? id : "", status ? status : ""});
  }
  if (result != SQLITE_DONE) {
    throw std::runtime_error(std::string("failed to query stale tasks: ") +
                             sqlite3_errmsg(db));
  }
  return tasks;
}

bool hasTerminalEvent(sqlite3 *db, Statement &query,
                      const std::string &taskId) {
  query.reset();
  bindText(db, query.get(), 1, taskId);
  const int result = sqlite3_step(query.get());
  if (result == SQLITE_ROW) {
    return true;
  }
  if (result != SQLITE_DONE) {
    throw std::runtime_error(std::string("failed to inspect terminal events: ") +
                             sqlite3_errmsg(db));
  }
  return false;
}

RecoveryEventIdState inspectRecoveryEventId(sqlite3 *db, Statement &query,
                                            const std::string &eventId,
                                            const std::string &taskId) {
  query.reset();
  bindText(db, query.get(), 1, eventId);
  const int result = sqlite3_step(query.get());
  if (result == SQLITE_DONE) {
    return RecoveryEventIdState::Available;
  }
  if (result != SQLITE_ROW) {
    throw std::runtime_error(
        std::string("failed to inspect crash recovery event id: ") +
        sqlite3_errmsg(db));
  }

  const auto *storedTask =
      reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 0));
  const auto *storedType =
      reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 1));
  const auto *storedMetadata =
      reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 2));
  const std::string metadata = storedMetadata ? storedMetadata : "";
  const bool matches = storedTask && taskId == storedTask && storedType &&
                       std::string(storedType) == "task_failed" &&
                       metadata.find("\"status\":\"interrupted\"") !=
                           std::string::npos &&
                       metadata.find(
                           "\"reason\":\"backend_crash_recovery\"") !=
                           std::string::npos;
  return matches ? RecoveryEventIdState::MatchingRecovery
                 : RecoveryEventIdState::Conflict;
}

} // namespace

TaskRecoveryReport recoverInterruptedTasks(sqlite3 *db) {
  if (!db) {
    throw std::invalid_argument("task recovery requires an open database");
  }

  execute(db, "BEGIN IMMEDIATE;", "failed to begin task recovery");
  bool transactionOpen = true;
  try {
    const auto staleTasks = findStaleTasks(db);
    TaskRecoveryReport report;
    Statement updateTask(
        db, std::string("UPDATE tasks SET status='interrupted', ") +
                "updated_at=CURRENT_TIMESTAMP WHERE id=? AND status IN " +
                kRecoverableStatuses + ";");
    Statement expirePermissions(
        db, "UPDATE permission_requests SET status='expired', "
            "resolved_at=CURRENT_TIMESTAMP WHERE task_id=? AND "
            "status='pending';");
    Statement terminalEvent(
        db, "SELECT 1 FROM task_events WHERE task_id=? AND type IN "
            "('task_completed','task_failed','task_cancelled') LIMIT 1;");
    Statement recoveryEventId(
        db, "SELECT task_id, type, metadata FROM task_events WHERE id=?;");
    Statement insertEvent(
        db, "INSERT INTO task_events "
            "(id, task_id, type, content, metadata) VALUES (?, ?, "
            "'task_failed', ?, ?);");

    const std::string content =
        "The backend exited unexpectedly while this task was executing. "
        "This task cannot be resumed automatically. Existing execution "
        "records were preserved.";

    for (const auto &task : staleTasks) {
      updateTask.reset();
      bindText(db, updateTask.get(), 1, task.id);
      stepDone(db, updateTask.get(), "failed to interrupt stale task");
      if (sqlite3_changes(db) != 1) {
        continue;
      }
      ++report.tasksInterrupted;

      expirePermissions.reset();
      bindText(db, expirePermissions.get(), 1, task.id);
      stepDone(db, expirePermissions.get(),
               "failed to expire stale permission requests");
      report.permissionsExpired +=
          static_cast<std::size_t>(sqlite3_changes(db));

      const std::string eventId = "crash_recovery:" + task.id;
      const auto eventIdState =
          inspectRecoveryEventId(db, recoveryEventId, eventId, task.id);
      if (eventIdState == RecoveryEventIdState::MatchingRecovery) {
        continue;
      }
      if (eventIdState == RecoveryEventIdState::Conflict) {
        throw std::runtime_error("crash recovery event id is already used: " +
                                 eventId);
      }
      if (hasTerminalEvent(db, terminalEvent, task.id)) {
        continue;
      }

      const std::string metadata =
          "{\"channel\":\"status\",\"status\":\"interrupted\","
          "\"reason\":\"backend_crash_recovery\",\"recoverable\":false,"
          "\"previous_status\":\"" +
          task.status + "\"}";
      insertEvent.reset();
      bindText(db, insertEvent.get(), 1, eventId);
      bindText(db, insertEvent.get(), 2, task.id);
      bindText(db, insertEvent.get(), 3, content);
      bindText(db, insertEvent.get(), 4, metadata);
      stepDone(db, insertEvent.get(),
               "failed to insert crash recovery terminal event");
      report.terminalEventsInserted +=
          static_cast<std::size_t>(sqlite3_changes(db));
    }

    execute(db, "COMMIT;", "failed to commit task recovery");
    transactionOpen = false;
    return report;
  } catch (...) {
    if (transactionOpen) {
      sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    throw;
  }
}

} // namespace codepilot

#include "infrastructure/storage/TaskRecovery.h"

#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void execute(sqlite3 *db, const char *sql) {
  char *error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : sqlite3_errmsg(db);
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

int scalarInt(sqlite3 *db, const std::string &sql) {
  sqlite3_stmt *stmt = nullptr;
  require(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK,
          sqlite3_errmsg(db));
  require(sqlite3_step(stmt) == SQLITE_ROW, sqlite3_errmsg(db));
  const int value = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return value;
}

std::string scalarText(sqlite3 *db, const std::string &sql) {
  sqlite3_stmt *stmt = nullptr;
  require(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK,
          sqlite3_errmsg(db));
  require(sqlite3_step(stmt) == SQLITE_ROW, sqlite3_errmsg(db));
  const auto *value =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const std::string result = value ? value : "";
  sqlite3_finalize(stmt);
  return result;
}

void insertTask(sqlite3 *db, const std::string &id,
                const std::string &status) {
  const std::string sql =
      "INSERT INTO tasks VALUES ('" + id + "','session','global','workspace',"
      "'goal','" + status + "','plan','original-step','created','updated');";
  execute(db, sql.c_str());
}

void createSchema(sqlite3 *db) {
  execute(db, R"SQL(
CREATE TABLE tasks (
  id TEXT PRIMARY KEY, session_id TEXT, global_id TEXT, workspace_id TEXT,
  goal TEXT, status TEXT, plan TEXT, current_step TEXT,
  created_at TEXT, updated_at TEXT
);
CREATE TABLE task_events (
  id TEXT PRIMARY KEY, task_id TEXT NOT NULL, type TEXT NOT NULL,
  content TEXT, metadata TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE permission_requests (
  id TEXT PRIMARY KEY, task_id TEXT NOT NULL, tool_name TEXT,
  risk_level TEXT, action TEXT, reason TEXT, status TEXT,
  created_at TEXT, resolved_at TEXT
);
CREATE TABLE execution_logs (
  id INTEGER PRIMARY KEY, task_id TEXT, type TEXT, content TEXT
);
CREATE TABLE tool_calls (
  id TEXT PRIMARY KEY, task_id TEXT, tool_name TEXT, arguments TEXT,
  success INTEGER, result TEXT, exit_code INTEGER, created_at TEXT
);
CREATE TABLE file_changes (
  id TEXT PRIMARY KEY, task_id TEXT, file_path TEXT, change_type TEXT,
  diff TEXT, created_at TEXT
);
CREATE TABLE task_contexts (
  task_id TEXT PRIMARY KEY, context_json TEXT, updated_at TEXT
);
)SQL");
}

void seed(sqlite3 *db) {
  insertTask(db, "running-task", "running");
  insertTask(db, "pending-task", "pending");
  insertTask(db, "created-task", "created");
  insertTask(db, "planning-task", "planning");
  insertTask(db, "paused-task", "paused");
  insertTask(db, "permission-task", "waiting_permission");
  insertTask(db, "existing-terminal-event", "running");
  insertTask(db, "same-recovery-event", "running");
  insertTask(db, "already-interrupted", "interrupted");
  insertTask(db, "completed-task", "completed");
  insertTask(db, "failed-task", "failed");
  insertTask(db, "cancelled-task", "cancelled");

  execute(db, R"SQL(
INSERT INTO task_events VALUES
  ('old-event','running-task','tool_finished','old','{}','before'),
  ('existing-failure','existing-terminal-event','task_failed','old failure','{}','before'),
  ('crash_recovery:same-recovery-event','same-recovery-event','task_failed','recovered','{"status":"interrupted","reason":"backend_crash_recovery"}','before'),
  ('completed-event','completed-task','task_completed','done','{}','before');
INSERT INTO permission_requests VALUES
  ('pending-permission','permission-task','shell','dangerous','run','why','pending','before',NULL),
  ('approved-permission','running-task','file','medium','write','why','approved','before','before'),
  ('terminal-task-permission','completed-task','shell','dangerous','run','why','pending','before',NULL);
INSERT INTO execution_logs VALUES (1,'running-task','step','kept');
INSERT INTO tool_calls VALUES
  ('tool-call','running-task','shell','{}',1,'kept',0,'before');
INSERT INTO file_changes VALUES
  ('file-change','running-task','file.txt','modified','kept','before');
INSERT INTO task_contexts VALUES
  ('running-task','{"messages":["kept"]}','before');
)SQL");
}

void verifyFirstRecovery(sqlite3 *db) {
  const auto report = codepilot::recoverInterruptedTasks(db);
  require(report.tasksInterrupted == 8, "all non-terminal tasks recovered");
  require(report.permissionsExpired == 1,
          "only pending permission on interrupted task expired");
  require(report.terminalEventsInserted == 6,
          "existing terminal event prevents duplicate insertion");

  for (const std::string id : {"running-task", "pending-task", "created-task",
                               "planning-task", "paused-task",
                               "permission-task", "existing-terminal-event",
                               "same-recovery-event"}) {
    require(scalarText(db, "SELECT status FROM tasks WHERE id='" + id +
                               "';") == "interrupted",
            id + " was not interrupted");
  }
  require(scalarText(db, "SELECT status FROM tasks WHERE "
                         "id='already-interrupted';") == "interrupted",
          "interrupted task was modified");
  require(scalarText(db, "SELECT updated_at FROM tasks WHERE "
                         "id='already-interrupted';") == "updated",
          "interrupted task timestamp was modified");
  for (const std::string id : {"completed-task", "failed-task",
                               "cancelled-task"}) {
    require(scalarText(db, "SELECT status FROM tasks WHERE id='" + id +
                               "';") == id.substr(0, id.find('-')),
            id + " changed unexpectedly");
  }

  require(scalarText(db, "SELECT status FROM permission_requests WHERE "
                         "id='pending-permission';") == "expired",
          "pending permission remains actionable");
  require(scalarText(db, "SELECT status FROM permission_requests WHERE "
                         "id='terminal-task-permission';") == "pending",
          "terminal task permission should remain historical and untouched");
  require(scalarText(db, "SELECT current_step FROM tasks WHERE "
                         "id='running-task';") == "original-step",
          "task execution details were overwritten");
  require(scalarInt(db, "SELECT COUNT(*) FROM execution_logs WHERE "
                        "task_id='running-task';") == 1,
          "execution log was not preserved");
  require(scalarInt(db, "SELECT COUNT(*) FROM tool_calls WHERE "
                        "task_id='running-task';") == 1,
          "tool call was not preserved");
  require(scalarInt(db, "SELECT COUNT(*) FROM file_changes WHERE "
                        "task_id='running-task';") == 1,
          "file change was not preserved");
  require(scalarInt(db, "SELECT COUNT(*) FROM task_contexts WHERE "
                        "task_id='running-task';") == 1,
          "task context was not preserved");
  require(scalarInt(db, "SELECT COUNT(*) FROM task_events WHERE "
                        "task_id='existing-terminal-event' AND type IN "
                        "('task_completed','task_failed','task_cancelled');") ==
              1,
          "terminal event was duplicated");
  require(scalarInt(db, "SELECT COUNT(*) FROM task_events WHERE "
                        "id='crash_recovery:same-recovery-event';") == 1,
          "matching recovery event id was duplicated");
  require(scalarText(db, "SELECT metadata FROM task_events WHERE "
                         "task_id='running-task' AND type='task_failed';")
                  .find("backend_crash_recovery") != std::string::npos,
          "recovery event lacks an explicit crash reason");
}

void verifyIdempotence(sqlite3 *db) {
  const int eventCount = scalarInt(db, "SELECT COUNT(*) FROM task_events;");
  const std::string taskUpdatedAt =
      scalarText(db, "SELECT updated_at FROM tasks WHERE id='running-task';");
  const std::string permissionResolvedAt =
      scalarText(db, "SELECT resolved_at FROM permission_requests WHERE "
                     "id='pending-permission';");
  const auto second = codepilot::recoverInterruptedTasks(db);
  const auto third = codepilot::recoverInterruptedTasks(db);
  require(second.tasksInterrupted == 0 && second.permissionsExpired == 0 &&
              second.terminalEventsInserted == 0,
          "second recovery changed the database");
  require(third.tasksInterrupted == 0 && third.permissionsExpired == 0 &&
              third.terminalEventsInserted == 0,
          "third recovery changed the database");
  require(scalarInt(db, "SELECT COUNT(*) FROM task_events;") == eventCount,
          "repeated recovery inserted terminal events");
  require(scalarText(db, "SELECT updated_at FROM tasks WHERE "
                         "id='running-task';") == taskUpdatedAt,
          "repeated recovery modified task timestamp");
  require(scalarText(db, "SELECT resolved_at FROM permission_requests WHERE "
                         "id='pending-permission';") == permissionResolvedAt,
          "repeated recovery modified expired permission");
}

void verifyConflictingEventIdRollsBack() {
  sqlite3 *db = nullptr;
  require(sqlite3_open(":memory:", &db) == SQLITE_OK,
          "failed to open rollback test database");
  try {
    createSchema(db);
    insertTask(db, "a-earlier-task", "running");
    insertTask(db, "z-conflict-task", "running");
    insertTask(db, "other-task", "completed");
    execute(db, R"SQL(
INSERT INTO permission_requests VALUES
  ('rollback-permission','a-earlier-task','shell','dangerous','run','why','pending','before',NULL);
INSERT INTO task_events VALUES
  ('crash_recovery:z-conflict-task','other-task','tool_finished','occupied','{}','before');
)SQL");

    bool threw = false;
    try {
      codepilot::recoverInterruptedTasks(db);
    } catch (const std::runtime_error &) {
      threw = true;
    }
    require(threw, "conflicting recovery event id did not fail");
    require(scalarText(db, "SELECT status FROM tasks WHERE "
                           "id='a-earlier-task';") == "running",
            "earlier task update was not rolled back");
    require(scalarText(db, "SELECT status FROM tasks WHERE "
                           "id='z-conflict-task';") == "running",
            "conflicting task update was not rolled back");
    require(scalarText(db, "SELECT status FROM permission_requests WHERE "
                           "id='rollback-permission';") == "pending",
            "permission expiration was not rolled back");
    require(scalarInt(db, "SELECT COUNT(*) FROM task_events;") == 1,
            "rollback left a partial terminal event");
    sqlite3_close(db);
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}

} // namespace

int main() {
  sqlite3 *db = nullptr;
  try {
    require(sqlite3_open(":memory:", &db) == SQLITE_OK,
            "failed to open temporary SQLite database");
    createSchema(db);
    seed(db);
    verifyFirstRecovery(db);
    verifyIdempotence(db);
    verifyConflictingEventIdRollsBack();
    sqlite3_close(db);
    std::cout << "Task recovery test passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    if (db) {
      sqlite3_close(db);
    }
    std::cerr << "Task recovery test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

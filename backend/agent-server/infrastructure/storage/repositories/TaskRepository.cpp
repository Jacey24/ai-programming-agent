#include "infrastructure/storage/repositories/TaskRepository.h"

#include <stdexcept>

TaskRepository::TaskRepository(sqlite3 *db) : db_(db) {}

void TaskRepository::initTable() {
  const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS tasks (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    global_id TEXT,
    workspace_id TEXT NOT NULL,
    goal TEXT NOT NULL,
    status TEXT NOT NULL,
    plan TEXT,
    current_step TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
)SQL";

  char *error_message = nullptr;
  const int exec_result =
      sqlite3_exec(db_, sql, nullptr, nullptr, &error_message);
  if (exec_result != SQLITE_OK) {
    const std::string error = error_message ? error_message : lastError();
    sqlite3_free(error_message);
    throw std::runtime_error(error);
  }

  const auto has_column = [&](const std::string &column_name) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(tasks);", -1, &stmt,
                           nullptr) != SQLITE_OK) {
      throw std::runtime_error(lastError());
    }

    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
      const auto *name =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
      if (name && column_name == name) {
        sqlite3_finalize(stmt);
        return true;
      }
    }

    if (step_result != SQLITE_DONE) {
      const std::string error = lastError();
      sqlite3_finalize(stmt);
      throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return false;
  };

  const auto add_column_if_missing = [&](const std::string &column_name,
                                         const char *alter_sql) {
    if (has_column(column_name)) {
      return;
    }

    char *alter_error = nullptr;
    if (sqlite3_exec(db_, alter_sql, nullptr, nullptr, &alter_error) !=
        SQLITE_OK) {
      const std::string error = alter_error ? alter_error : lastError();
      sqlite3_free(alter_error);
      throw std::runtime_error(error);
    }
  };

  add_column_if_missing("session_id",
                        "ALTER TABLE tasks ADD COLUMN session_id TEXT;");
  add_column_if_missing("global_id",
                        "ALTER TABLE tasks ADD COLUMN global_id TEXT;");
}

TaskRecord TaskRepository::createTask(const std::string &id,
                                      const std::string &session_id,
                                      const std::string &global_id,
                                      const std::string &workspace_id,
                                      const std::string &goal,
                                      const std::string &created_at,
                                      const std::string &updated_at) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO tasks (id, session_id, global_id, workspace_id, goal, "
      "status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, global_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, goal.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, "created", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, updated_at.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return TaskRecord{
      id, session_id, global_id, workspace_id, goal,       "created",
      "", "",         created_at,   updated_at,
  };
}

std::optional<TaskRecord> TaskRepository::findById(const std::string &task_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, global_id, workspace_id, goal, status, plan, "
      "current_step, created_at, updated_at FROM tasks WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  if (step_result != SQLITE_ROW) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  const auto *id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  const auto *session_id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const auto *global_id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const auto *workspace_id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const auto *goal =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const auto *status =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  const auto *plan =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
  const auto *current_step =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
  const auto *created_at =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
  const auto *updated_at =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));

  TaskRecord task{
      id ? id : "",
      session_id ? session_id : "",
      global_id ? global_id : "",
      workspace_id ? workspace_id : "",
      goal ? goal : "",
      status ? status : "",
      plan ? plan : "",
      current_step ? current_step : "",
      created_at ? created_at : "",
      updated_at ? updated_at : "",
  };

  sqlite3_finalize(stmt);
  return task;
}

std::vector<TaskRecord> TaskRepository::listRecent(int limit) {
  if (limit <= 0 || limit > 100) {
    limit = 20;
  }

  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, global_id, workspace_id, goal, status, plan, "
      "current_step, created_at, updated_at FROM tasks ORDER BY created_at "
      "DESC LIMIT ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_int(stmt, 1, limit);

  std::vector<TaskRecord> tasks;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const auto *session_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *global_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *workspace_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const auto *goal =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const auto *status =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const auto *plan =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    const auto *current_step =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    const auto *created_at =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    const auto *updated_at =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));

    tasks.push_back(TaskRecord{
        id ? id : "",
        session_id ? session_id : "",
        global_id ? global_id : "",
        workspace_id ? workspace_id : "",
        goal ? goal : "",
        status ? status : "",
        plan ? plan : "",
        current_step ? current_step : "",
        created_at ? created_at : "",
        updated_at ? updated_at : "",
    });
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return tasks;
}

std::vector<TaskRecord>
TaskRepository::findBySessionId(const std::string &session_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, global_id, workspace_id, goal, status, plan, "
      "current_step, created_at, updated_at FROM tasks WHERE session_id = ? "
      "ORDER BY created_at DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<TaskRecord> tasks;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const auto *sid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *gid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *wid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const auto *goal =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const auto *status =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const auto *plan =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    const auto *cs =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    const auto *ca =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    const auto *ua =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));

    tasks.push_back(TaskRecord{
        id ? id : "",
        sid ? sid : "",
        gid ? gid : "",
        wid ? wid : "",
        goal ? goal : "",
        status ? status : "",
        plan ? plan : "",
        cs ? cs : "",
        ca ? ca : "",
        ua ? ua : "",
    });
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return tasks;
}

std::vector<TaskRecord>
TaskRepository::findByGlobalId(const std::string &global_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, global_id, workspace_id, goal, status, plan, "
      "current_step, created_at, updated_at FROM tasks WHERE global_id = ? "
      "ORDER BY created_at DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, global_id.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<TaskRecord> tasks;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const auto *sid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *gid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *wid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const auto *goal =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const auto *status =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const auto *plan =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    const auto *cs =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    const auto *ca =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    const auto *ua =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));

    tasks.push_back(TaskRecord{
        id ? id : "",
        sid ? sid : "",
        gid ? gid : "",
        wid ? wid : "",
        goal ? goal : "",
        status ? status : "",
        plan ? plan : "",
        cs ? cs : "",
        ca ? ca : "",
        ua ? ua : "",
    });
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return tasks;
}

void TaskRepository::updateExecution(const std::string &task_id,
                                     const std::string &status,
                                     const std::string &plan,
                                     const std::string &current_step,
                                     const std::string &updated_at) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE tasks SET status = ?, plan = ?, current_step = ?, updated_at = ? "
      "WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, plan.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, current_step.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, updated_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, task_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
}

std::string TaskRepository::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

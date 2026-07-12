#include "infrastructure/storage/repositories/WorkspaceRepository.h"

#include <stdexcept>

WorkspaceRepository::WorkspaceRepository(sqlite3 *db) : db_(db) {}

void WorkspaceRepository::initTable() {
  const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS workspaces (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    created_at TEXT NOT NULL
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
}

WorkspaceRecord WorkspaceRepository::create(const std::string &id,
                                            const std::string &name,
                                            const std::string &path,
                                            const std::string &created_at) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO workspaces (id, name, path, created_at) VALUES (?, ?, ?, "
      "?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, created_at.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return WorkspaceRecord{id, name, path, created_at};
}

std::optional<WorkspaceRecord>
WorkspaceRepository::findById(const std::string &workspace_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, name, path, created_at FROM workspaces WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);

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
  const auto *name =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  const auto *path =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const auto *created_at =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));

  WorkspaceRecord workspace{
      id ? id : "",
      name ? name : "",
      path ? path : "",
      created_at ? created_at : "",
  };

  sqlite3_finalize(stmt);
  return workspace;
}

std::vector<WorkspaceRecord> WorkspaceRepository::listAll() {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, name, path, created_at FROM workspaces ORDER BY created_at "
      "DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  std::vector<WorkspaceRecord> workspaces;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const auto *name =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *path =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *created_at =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));

    workspaces.push_back(WorkspaceRecord{
        id ? id : "",
        name ? name : "",
        path ? path : "",
        created_at ? created_at : "",
    });
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return workspaces;
}

std::string WorkspaceRepository::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}
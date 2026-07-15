#include "infrastructure/storage/repositories/SessionRepository.h"

#include <stdexcept>

SessionRepository::SessionRepository(sqlite3 *db) : db_(db) {}

void SessionRepository::initTable() {
  const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    alias TEXT DEFAULT '',
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    workspace_id TEXT DEFAULT '',
    summary TEXT DEFAULT '',
    summary_updated_at TEXT DEFAULT '',
    last_active_at TEXT DEFAULT ''
);
)SQL";

  char *error_message = nullptr;
  int exec_result = sqlite3_exec(db_, sql, nullptr, nullptr, &error_message);
  if (exec_result != SQLITE_OK) {
    const std::string error = error_message ? error_message : lastError();
    sqlite3_free(error_message);
    throw std::runtime_error(error);
  }

  // Migration: add alias column if missing (for databases created before this
  // field existed)
  const char *migration_sql =
      "ALTER TABLE sessions ADD COLUMN alias TEXT DEFAULT '';";
  sqlite3_exec(db_, migration_sql, nullptr, nullptr,
               nullptr); // ignore error if column already exists
}

SessionRecord SessionRepository::createSession(const std::string &id,
                                               const std::string &title,
                                               const std::string &created_at,
                                               const std::string &updated_at) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO sessions (id, title, created_at, updated_at) VALUES (?, ?, "
      "?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, updated_at.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return SessionRecord{id, title, "", "", "", "", "", created_at, updated_at};
}

std::optional<SessionRecord>
SessionRepository::findById(const std::string &session_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, title, alias, workspace_id, summary, summary_updated_at, "
      "last_active_at, created_at, updated_at FROM sessions WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

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

  auto col = [&](int idx) -> std::string {
    const auto *t =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx));
    return t ? t : "";
  };

  SessionRecord session{col(0), col(1), col(2), col(3), col(4),
                        col(5), col(6), col(7), col(8)};

  sqlite3_finalize(stmt);
  return session;
}

std::vector<SessionRecord>
SessionRepository::findByWorkspaceId(const std::string &workspace_id,
                                     const std::string &orderBy, int limit) {
  sqlite3_stmt *stmt = nullptr;
  std::string sql =
      "SELECT id, title, alias, workspace_id, summary, summary_updated_at, "
      "last_active_at, created_at, updated_at FROM sessions WHERE "
      "workspace_id = ? ORDER BY " +
      orderBy;
  if (limit > 0) {
    sql += " LIMIT " + std::to_string(limit);
  }
  sql += ";";

  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);

  auto col = [&](int idx) -> std::string {
    const auto *t =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx));
    return t ? t : "";
  };

  std::vector<SessionRecord> sessions;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    sessions.push_back(SessionRecord{col(0), col(1), col(2), col(3), col(4),
                                     col(5), col(6), col(7), col(8)});
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return sessions;
}

bool SessionRepository::deleteById(const std::string &session_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM sessions WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  const bool deleted = (step == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return deleted;
}

std::string SessionRepository::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

std::vector<SessionRecord> SessionRepository::listAll() {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, title, alias, workspace_id, summary, summary_updated_at, "
      "last_active_at, created_at, updated_at FROM sessions ORDER BY "
      "created_at DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  auto col = [&](int idx) -> std::string {
    const auto *t =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx));
    return t ? t : "";
  };

  std::vector<SessionRecord> sessions;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    sessions.push_back(SessionRecord{col(0), col(1), col(2), col(3), col(4),
                                     col(5), col(6), col(7), col(8)});
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return sessions;
}

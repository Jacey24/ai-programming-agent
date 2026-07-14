#include "MigrationRunner.h"

#include <cstring>
#include <stdexcept>

namespace codepilot {

MigrationRunner::MigrationRunner(sqlite3 *db) : db_(db) {}

std::string MigrationRunner::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "database not open";
}

void MigrationRunner::ensureSchemaMigrationsTable() {
  const char *sql = "CREATE TABLE IF NOT EXISTS schema_migrations ("
                    "  version     INTEGER PRIMARY KEY,"
                    "  name        TEXT NOT NULL,"
                    "  applied_at  TEXT NOT NULL DEFAULT (datetime('now'))"
                    ");";

  char *err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : lastError();
    sqlite3_free(err);
    throw std::runtime_error("Failed to create schema_migrations: " + msg);
  }
}

int MigrationRunner::currentVersion() {
  int version = 0;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;";

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      version = sqlite3_column_int(stmt, 0);
    }
  }
  sqlite3_finalize(stmt);
  return version;
}

std::vector<MigrationRunner::Migration>
MigrationRunner::getPendingMigrations() {
  int current = 0;
  try {
    current = currentVersion();
  } catch (...) {
    current = 0;
  }

  auto all = builtinMigrations();
  std::vector<Migration> pending;
  for (auto &m : all) {
    if (m.version > current) {
      pending.push_back(std::move(m));
    }
  }
  return pending;
}

bool MigrationRunner::applyMigration(const Migration &m) {
  char *err = nullptr;

  if (sqlite3_exec(db_, m.sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : lastError();
    sqlite3_free(err);
    return false;
  }

  sqlite3_stmt *stmt = nullptr;
  const char *insertSql =
      "INSERT INTO schema_migrations (version, name) VALUES (?, ?);";

  if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, m.version);
  sqlite3_bind_text(stmt, 2, m.name.c_str(), -1, SQLITE_TRANSIENT);

  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}

bool MigrationRunner::migrate() {
  try {
    ensureSchemaMigrationsTable();
  } catch (const std::exception &) {
    return false;
  }

  auto pending = getPendingMigrations();
  if (pending.empty()) {
    return true;
  }

  if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) !=
      SQLITE_OK) {
    return false;
  }

  bool allOk = true;
  for (auto &m : pending) {
    if (!applyMigration(m)) {
      allOk = false;
      break;
    }
  }

  if (allOk) {
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  } else {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  return allOk;
}

// ════════════════════════════════════════════════════════════
// 内置迁移定义（使用 C++11 字符串拼接，避免原始字符串字面量）
// ════════════════════════════════════════════════════════════
std::vector<MigrationRunner::Migration> MigrationRunner::builtinMigrations() {
  std::vector<Migration> migrations;

  // V001: 基线
  migrations.push_back({1, "baseline",
                        "CREATE TABLE IF NOT EXISTS globals ("
                        "  id TEXT PRIMARY KEY,"
                        "  name TEXT NOT NULL,"
                        "  description TEXT DEFAULT '',"
                        "  created_at TEXT NOT NULL,"
                        "  updated_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS global_context ("
                        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "  global_id TEXT NOT NULL,"
                        "  source_task_id TEXT NOT NULL,"
                        "  type TEXT NOT NULL,"
                        "  content TEXT NOT NULL,"
                        "  created_at TEXT NOT NULL,"
                        "  FOREIGN KEY (global_id) REFERENCES globals(id)"
                        ");"

                        "CREATE TABLE IF NOT EXISTS sessions ("
                        "  id TEXT PRIMARY KEY,"
                        "  title TEXT NOT NULL,"
                        "  created_at TEXT NOT NULL,"
                        "  updated_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS workspaces ("
                        "  id TEXT PRIMARY KEY,"
                        "  name TEXT NOT NULL,"
                        "  path TEXT NOT NULL,"
                        "  created_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS tasks ("
                        "  id TEXT PRIMARY KEY,"
                        "  global_id TEXT NOT NULL,"
                        "  workspace_id TEXT NOT NULL,"
                        "  goal TEXT NOT NULL,"
                        "  status TEXT DEFAULT 'pending',"
                        "  plan TEXT DEFAULT '',"
                        "  current_step TEXT DEFAULT '',"
                        "  created_at TEXT NOT NULL,"
                        "  updated_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS task_events ("
                        "  id TEXT PRIMARY KEY,"
                        "  task_id TEXT NOT NULL,"
                        "  type TEXT NOT NULL,"
                        "  content TEXT NOT NULL,"
                        "  metadata TEXT DEFAULT '{}',"
                        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP"
                        ");"

                        "CREATE TABLE IF NOT EXISTS tool_calls ("
                        "  id TEXT PRIMARY KEY,"
                        "  task_id TEXT NOT NULL,"
                        "  tool_name TEXT NOT NULL,"
                        "  arguments TEXT DEFAULT '{}',"
                        "  success INTEGER DEFAULT 0,"
                        "  result TEXT DEFAULT '',"
                        "  exit_code INTEGER DEFAULT 0,"
                        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP"
                        ");"

                        "CREATE TABLE IF NOT EXISTS permission_requests ("
                        "  id TEXT PRIMARY KEY,"
                        "  task_id TEXT NOT NULL,"
                        "  tool_name TEXT NOT NULL,"
                        "  risk_level TEXT NOT NULL,"
                        "  action TEXT NOT NULL,"
                        "  reason TEXT NOT NULL,"
                        "  status TEXT DEFAULT 'pending',"
                        "  created_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS file_changes ("
                        "  id TEXT PRIMARY KEY,"
                        "  task_id TEXT NOT NULL,"
                        "  file_path TEXT NOT NULL,"
                        "  change_type TEXT NOT NULL,"
                        "  diff TEXT DEFAULT '',"
                        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP"
                        ");"

                        "CREATE TABLE IF NOT EXISTS execution_logs ("
                        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "  task_id TEXT NOT NULL,"
                        "  type TEXT NOT NULL,"
                        "  content TEXT NOT NULL,"
                        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP"
                        ");"

                        "CREATE TABLE IF NOT EXISTS task_contexts ("
                        "  task_id TEXT PRIMARY KEY,"
                        "  context_json TEXT NOT NULL,"
                        "  updated_at TEXT NOT NULL"
                        ");"

                        "CREATE TABLE IF NOT EXISTS system_health ("
                        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
                        "  service TEXT NOT NULL,"
                        "  checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
                        ");"});

  // V002: Workspace/Session 增强
  migrations.push_back(
      {2, "workspace_enhance",
       "ALTER TABLE workspaces ADD COLUMN description TEXT DEFAULT '';"
       "ALTER TABLE workspaces ADD COLUMN last_opened_at TEXT DEFAULT '';"

       "ALTER TABLE sessions ADD COLUMN workspace_id TEXT DEFAULT '';"
       "ALTER TABLE sessions ADD COLUMN summary TEXT DEFAULT '';"
       "ALTER TABLE sessions ADD COLUMN summary_updated_at TEXT DEFAULT '';"

       "ALTER TABLE tasks ADD COLUMN user_message_id TEXT DEFAULT '';"
       "ALTER TABLE tasks ADD COLUMN assistant_message_id TEXT DEFAULT '';"});

  // V003: Messages 表
  migrations.push_back(
      {3, "messages",
       "CREATE TABLE IF NOT EXISTS messages ("
       "  id TEXT PRIMARY KEY,"
       "  session_id TEXT NOT NULL,"
       "  task_id TEXT DEFAULT '',"
       "  role TEXT NOT NULL CHECK (role IN "
       "('user','assistant','system','tool')),"
       "  content TEXT NOT NULL,"
       "  status TEXT NOT NULL DEFAULT 'complete',"
       "  token_count INTEGER DEFAULT 0,"
       "  metadata TEXT DEFAULT '{}',"
       "  created_at TEXT NOT NULL,"
       "  updated_at TEXT NOT NULL"
       ");"

       "CREATE INDEX IF NOT EXISTS idx_messages_session ON "
       "messages(session_id, created_at);"
       "CREATE INDEX IF NOT EXISTS idx_messages_task ON messages(task_id);"

       "CREATE TABLE IF NOT EXISTS message_context_files ("
       "  id TEXT PRIMARY KEY,"
       "  message_id TEXT NOT NULL,"
       "  workspace_id TEXT NOT NULL,"
       "  relative_path TEXT NOT NULL,"
       "  content_hash TEXT DEFAULT '',"
       "  file_size INTEGER DEFAULT 0,"
       "  created_at TEXT NOT NULL,"
       "  FOREIGN KEY (message_id) REFERENCES messages(id)"
       ");"

       "CREATE TABLE IF NOT EXISTS message_attachments ("
       "  message_id TEXT NOT NULL,"
       "  attachment_id TEXT NOT NULL,"
       "  PRIMARY KEY (message_id, attachment_id)"
       ");"});

  // V004: Conversation \u2194 Workspace \u7ed1\u5b9a\u589e\u5f3a
  migrations.push_back(
      {4, "session_workspace_binding",
       "ALTER TABLE workspaces ADD COLUMN permissions_config TEXT DEFAULT '{}';"
       "ALTER TABLE sessions ADD COLUMN last_active_at TEXT DEFAULT '';"});

  return migrations;
}

} // namespace codepilot
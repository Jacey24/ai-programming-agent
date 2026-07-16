#include "MigrationRunner.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace codepilot {

namespace {

std::string normalizeType(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }),
              value.end());
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}

std::string quoteIdentifier(const std::string &identifier) {
  std::string quoted = "\"";
  for (const char ch : identifier) {
    quoted += ch;
    if (ch == '"') {
      quoted += '"';
    }
  }
  return quoted + "\"";
}

MigrationRunner::MigrationStep sqlStep(std::string sql) {
  return {std::move(sql), false, {}};
}

MigrationRunner::MigrationStep addColumnStep(
    std::string sql, std::string table, std::string column, std::string type,
    bool notNull, std::string defaultValue) {
  return {std::move(sql),
          true,
          {std::move(table), std::move(column), std::move(type), notNull,
           std::move(defaultValue)}};
}

} // namespace

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
  ensureSchemaMigrationsTable();
  int version = 0;
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;";

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to query migration version: " +
                             lastError());
  }
  const int result = sqlite3_step(stmt);
  if (result == SQLITE_ROW) {
    version = sqlite3_column_int(stmt, 0);
  } else {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error("Failed to read migration version: " + error);
  }
  sqlite3_finalize(stmt);
  return version;
}

std::map<int, std::string> MigrationRunner::appliedMigrations() {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT version, name FROM schema_migrations;", -1,
                         &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to query applied migrations: " +
                             lastError());
  }

  std::map<int, std::string> applied;
  int result = SQLITE_ROW;
  while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    applied.emplace(sqlite3_column_int(stmt, 0), name ? name : "");
  }
  if (result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error("Failed to read applied migrations: " + error);
  }
  sqlite3_finalize(stmt);
  return applied;
}

void MigrationRunner::executeRequired(const std::string &sql,
                                      const std::string &context) {
  char *error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : lastError();
    sqlite3_free(error);
    throw std::runtime_error(context + ": " + message);
  }
}

bool MigrationRunner::columnMatches(const ColumnRequirement &requirement,
                                    bool &columnExists) {
  columnExists = false;
  const std::string sql =
      "PRAGMA table_info(" + quoteIdentifier(requirement.table) + ");";
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to inspect migration column " +
                             requirement.table + "." + requirement.column +
                             ": " + lastError());
  }

  int result = SQLITE_ROW;
  while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    if (!name || requirement.column != name) {
      continue;
    }
    columnExists = true;
    const auto *type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *defaultValue =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const bool matches =
        normalizeType(type ? type : "") == normalizeType(requirement.type) &&
        (sqlite3_column_int(stmt, 3) != 0) == requirement.notNull &&
        std::string(defaultValue ? defaultValue : "") ==
            requirement.defaultValue;
    sqlite3_finalize(stmt);
    return matches;
  }
  if (result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error("Failed to inspect migration column " +
                             requirement.table + "." + requirement.column +
                             ": " + error);
  }
  sqlite3_finalize(stmt);
  return false;
}

void MigrationRunner::applyStep(const Migration &migration,
                                const MigrationStep &step, size_t stepIndex) {
  const std::string context = "Migration " + std::to_string(migration.version) +
                              " step " + std::to_string(stepIndex + 1);
  if (!step.hasColumnRequirement) {
    executeRequired(step.sql, context);
    return;
  }

  bool exists = false;
  if (columnMatches(step.columnRequirement, exists)) {
    return;
  }
  if (exists) {
    throw std::runtime_error(
        context + " found an incompatible existing column " +
        step.columnRequirement.table + "." + step.columnRequirement.column);
  }

  executeRequired(step.sql, context);
  if (!columnMatches(step.columnRequirement, exists) || !exists) {
    throw std::runtime_error(context + " did not create the expected column " +
                             step.columnRequirement.table + "." +
                             step.columnRequirement.column);
  }
}

void MigrationRunner::applyMigration(const Migration &migration) {
  executeRequired("BEGIN IMMEDIATE TRANSACTION;",
                  "Failed to begin migration transaction");
  try {
    for (size_t index = 0; index < migration.steps.size(); ++index) {
      applyStep(migration, migration.steps[index], index);
    }

    sqlite3_stmt *stmt = nullptr;
    const char *insertSql =
        "INSERT OR IGNORE INTO schema_migrations (version, name) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare migration record: " +
                               lastError());
    }
    sqlite3_bind_int(stmt, 1, migration.version);
    sqlite3_bind_text(stmt, 2, migration.name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      const std::string error = lastError();
      sqlite3_finalize(stmt);
      throw std::runtime_error("Failed to record migration: " + error);
    }
    sqlite3_finalize(stmt);
    executeRequired("COMMIT;", "Failed to commit migration transaction");
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

bool MigrationRunner::migrate() {
  ensureSchemaMigrationsTable();
  auto applied = appliedMigrations();
  for (const auto &migration : builtinMigrations()) {
    const auto existing = applied.find(migration.version);
    if (existing != applied.end()) {
      if (existing->second != migration.name) {
        throw std::runtime_error("Migration record name mismatch for version " +
                                 std::to_string(migration.version));
      }
    }
    // Reconcile every migration with the real schema, even when a record is
    // already present. CREATE IF NOT EXISTS and checked ADD COLUMN steps make
    // this safe while repairing incomplete or externally modified schemas.
    applyMigration(migration);
  }
  return true;
}

// ════════════════════════════════════════════════════════════
// 内置迁移定义（使用 C++11 字符串拼接，避免原始字符串字面量）
// ════════════════════════════════════════════════════════════
std::vector<MigrationRunner::Migration> MigrationRunner::builtinMigrations() {
  std::vector<Migration> migrations;

  // V001: 基线
  migrations.push_back({1, "baseline", {sqlStep(
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
                        ");")}});

  // V002: Workspace/Session 增强
  migrations.push_back({
      2,
      "workspace_enhance",
      {addColumnStep(
           "ALTER TABLE workspaces ADD COLUMN description TEXT DEFAULT '';",
           "workspaces", "description", "TEXT", false, "''"),
       addColumnStep(
           "ALTER TABLE workspaces ADD COLUMN last_opened_at TEXT DEFAULT '';",
           "workspaces", "last_opened_at", "TEXT", false, "''"),
       addColumnStep(
           "ALTER TABLE sessions ADD COLUMN workspace_id TEXT DEFAULT '';",
           "sessions", "workspace_id", "TEXT", false, "''"),
       addColumnStep("ALTER TABLE sessions ADD COLUMN summary TEXT DEFAULT '';",
                     "sessions", "summary", "TEXT", false, "''"),
       addColumnStep(
           "ALTER TABLE sessions ADD COLUMN summary_updated_at TEXT DEFAULT '';",
           "sessions", "summary_updated_at", "TEXT", false, "''"),
       addColumnStep(
           "ALTER TABLE tasks ADD COLUMN user_message_id TEXT DEFAULT '';",
           "tasks", "user_message_id", "TEXT", false, "''"),
       addColumnStep(
           "ALTER TABLE tasks ADD COLUMN assistant_message_id TEXT DEFAULT '';",
           "tasks", "assistant_message_id", "TEXT", false, "''")}});

  // V003: Messages 表
  migrations.push_back(
      {3, "messages", {sqlStep(
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
       ");")}});

  // V004: Conversation \u2194 Workspace \u7ed1\u5b9a\u589e\u5f3a
  migrations.push_back({
      4,
      "session_workspace_binding",
      {addColumnStep(
           "ALTER TABLE workspaces ADD COLUMN permissions_config TEXT DEFAULT '{}';",
           "workspaces", "permissions_config", "TEXT", false, "'{}'"),
       addColumnStep(
           "ALTER TABLE sessions ADD COLUMN last_active_at TEXT DEFAULT '';",
           "sessions", "last_active_at", "TEXT", false, "''")}});

  return migrations;
}

} // namespace codepilot

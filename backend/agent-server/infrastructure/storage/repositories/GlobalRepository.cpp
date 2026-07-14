#include "infrastructure/storage/repositories/GlobalRepository.h"

#include <stdexcept>

GlobalRepository::GlobalRepository(sqlite3 *db) : db_(db) {}

void GlobalRepository::initTable() {
  const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS globals (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    workspace_id TEXT DEFAULT '',
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

  // 兼容迁移：为已有数据库添加 workspace_id 列
  const char *migrate_sql =
      "ALTER TABLE globals ADD COLUMN workspace_id TEXT DEFAULT ''";
  sqlite3_exec(db_, migrate_sql, nullptr, nullptr, nullptr);
}

void GlobalRepository::initContextTable() {
  const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS global_context (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    global_id TEXT NOT NULL,
    source_task_id TEXT NOT NULL,
    type TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY (global_id) REFERENCES globals(id)
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

GlobalRecord GlobalRepository::createGlobal(const std::string &id,
                                            const std::string &name,
                                            const std::string &description,
                                            const std::string &created_at,
                                            const std::string &updated_at,
                                            const std::string &workspace_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT INTO globals (id, name, description, workspace_id, "
                    "created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, updated_at.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return GlobalRecord{id,           name,       description,
                      workspace_id, created_at, updated_at};
}

std::optional<GlobalRecord>
GlobalRepository::findById(const std::string &global_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, name, description, workspace_id, created_at, "
                    "updated_at FROM globals WHERE "
                    "id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, global_id.c_str(), -1, SQLITE_TRANSIENT);

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
  const auto *description =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  const auto *ws_id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  const auto *created_at =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  const auto *updated_at =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

  GlobalRecord global{
      id ? id : "",
      name ? name : "",
      description ? description : "",
      ws_id ? ws_id : "",
      created_at ? created_at : "",
      updated_at ? updated_at : "",
  };

  sqlite3_finalize(stmt);
  return global;
}

bool GlobalRepository::deleteById(const std::string &global_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "DELETE FROM globals WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, global_id.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  const bool deleted = (step == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return deleted;
}

int GlobalRepository::count() {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT COUNT(*) FROM globals;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

std::string GlobalRepository::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

std::vector<GlobalRecord> GlobalRepository::listAll() {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, name, description, workspace_id, created_at, "
                    "updated_at FROM globals ORDER "
                    "BY created_at DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  std::vector<GlobalRecord> globals;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    const auto *id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const auto *name =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *desc =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *ws_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const auto *created =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const auto *updated =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

    globals.push_back(GlobalRecord{
        id ? id : "",
        name ? name : "",
        desc ? desc : "",
        ws_id ? ws_id : "",
        created ? created : "",
        updated ? updated : "",
    });
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return globals;
}

// ── Global Context ──

void GlobalRepository::saveContext(const std::string &global_id,
                                   const std::string &source_task_id,
                                   const std::string &type,
                                   const std::string &content,
                                   const std::string &created_at) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO global_context (global_id, source_task_id, type, content, "
      "created_at) VALUES (?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, global_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, source_task_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, created_at.c_str(), -1, SQLITE_TRANSIENT);

  const int step_result = sqlite3_step(stmt);
  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
}

std::vector<GlobalContextRecord>
GlobalRepository::getContextByGlobalId(const std::string &global_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, global_id, source_task_id, type, content, created_at FROM "
      "global_context WHERE global_id = ? ORDER BY created_at DESC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }

  sqlite3_bind_text(stmt, 1, global_id.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<GlobalContextRecord> records;
  int step_result = SQLITE_ROW;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    GlobalContextRecord rec;
    rec.id = sqlite3_column_int64(stmt, 0);
    const auto *gid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    const auto *stid =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const auto *typ =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const auto *cont =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const auto *cat =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    rec.global_id = gid ? gid : "";
    rec.source_task_id = stid ? stid : "";
    rec.type = typ ? typ : "";
    rec.content = cont ? cont : "";
    rec.created_at = cat ? cat : "";
    records.push_back(rec);
  }

  if (step_result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }

  sqlite3_finalize(stmt);
  return records;
}

// ★ 确保默认 Global 存在
std::string GlobalRepository::ensureDefaultGlobal() {
  if (count() > 0) {
    // 已经存在 Global，返回第一个
    auto all = listAll();
    if (!all.empty()) {
      return all.front().id;
    }
  }

  // 创建默认 Global
  const std::string now = "1970-01-01T00:00:00Z"; // 占位，由调用方传入
  // 实际时间由 facade 层提供
  return "";
}
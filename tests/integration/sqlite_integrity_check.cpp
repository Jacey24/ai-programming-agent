#include <sqlite3.h>

#include <iostream>
#include <string>

namespace {
bool scalar(sqlite3 *db, const char *sql, std::string &value) {
  sqlite3_stmt *statement = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }
  const bool ok = sqlite3_step(statement) == SQLITE_ROW;
  if (ok) {
    const auto *text = sqlite3_column_text(statement, 0);
    value = text ? reinterpret_cast<const char *>(text) : "";
  }
  sqlite3_finalize(statement);
  return ok;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: sqlite-integrity-check <database> [task-id ...]\n";
    return 2;
  }

  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(argv[1], &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    std::cerr << "unable to open database: " << (db ? sqlite3_errmsg(db) : "unknown") << "\n";
    if (db) sqlite3_close(db);
    return 1;
  }

  std::string integrity;
  std::string duplicateCount;
  bool ok = scalar(db, "PRAGMA integrity_check", integrity) && integrity == "ok" &&
            scalar(db, "SELECT count(*) FROM (SELECT id FROM tasks GROUP BY id HAVING count(*) > 1)", duplicateCount) &&
            duplicateCount == "0";

  sqlite3_stmt *task = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM tasks WHERE id = ?1", -1, &task, nullptr) != SQLITE_OK) {
    ok = false;
  } else {
    for (int i = 2; i < argc; ++i) {
      sqlite3_reset(task);
      sqlite3_clear_bindings(task);
      sqlite3_bind_text(task, 1, argv[i], -1, SQLITE_TRANSIENT);
      if (sqlite3_step(task) != SQLITE_ROW || sqlite3_column_int(task, 0) != 1) {
        std::cerr << "missing or duplicate task: " << argv[i] << "\n";
        ok = false;
      }
    }
    sqlite3_finalize(task);
  }

  sqlite3_close(db);
  std::cout << "{\"integrity_check\":\"" << integrity
            << "\",\"duplicate_task_ids\":" << (duplicateCount.empty() ? "null" : duplicateCount)
            << ",\"requested_task_count\":" << (argc - 2)
            << ",\"passed\":" << (ok ? "true" : "false") << "}\n";
  return ok ? 0 : 1;
}

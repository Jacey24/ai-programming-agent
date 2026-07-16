#include <sqlite3.h>

#include <iostream>
#include <string>

namespace {

int fail(sqlite3 *db, const std::string &operation) {
  std::cerr << operation << " failed: "
            << (db ? sqlite3_errmsg(db) : "database not open") << '\n';
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: sqlite-migration-test-helper <database> "
                 "<exec|scalar> <sql>\n";
    return 2;
  }

  sqlite3 *db = nullptr;
  if (sqlite3_open(argv[1], &db) != SQLITE_OK) {
    const int result = fail(db, "open");
    sqlite3_close(db);
    return result;
  }

  const std::string mode = argv[2];
  const std::string sql = argv[3];
  if (mode == "exec") {
    char *error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
      std::cerr << "exec failed: "
                << (error ? error : sqlite3_errmsg(db)) << '\n';
      sqlite3_free(error);
      sqlite3_close(db);
      return 1;
    }
  } else if (mode == "scalar") {
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK) {
      const int result = fail(db, "prepare");
      sqlite3_close(db);
      return result;
    }
    const int step = sqlite3_step(statement);
    if (step != SQLITE_ROW) {
      const int result = fail(db, "step");
      sqlite3_finalize(statement);
      sqlite3_close(db);
      return result;
    }
    const auto *value = sqlite3_column_text(statement, 0);
    if (value) {
      std::cout << reinterpret_cast<const char *>(value);
    }
    sqlite3_finalize(statement);
  } else {
    std::cerr << "unknown mode: " << mode << '\n';
    sqlite3_close(db);
    return 2;
  }

  if (sqlite3_close(db) != SQLITE_OK) {
    return fail(db, "close");
  }
  return 0;
}

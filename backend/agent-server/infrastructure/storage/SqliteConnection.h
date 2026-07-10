#pragma once

#include <sqlite3.h>

namespace codepilot {

inline int openSqliteConnection(const char* path, sqlite3** db) {
    const int result = sqlite3_open(path, db);
    if (result == SQLITE_OK && *db) {
        sqlite3_busy_timeout(*db, 5000);
    }
    return result;
}

inline void configureSqliteDatabase(sqlite3* db) {
    if (!db) {
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
}

} // namespace codepilot

#pragma once

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace codepilot {

inline int openSqliteConnection(const char* path, sqlite3** db) {
    const int result = sqlite3_open(path, db);
    if (result == SQLITE_OK && *db) {
        const int timeoutResult = sqlite3_busy_timeout(*db, 5000);
        if (timeoutResult != SQLITE_OK) {
            return timeoutResult;
        }
    }
    return result;
}

inline void configureSqliteDatabase(sqlite3* db) {
    if (!db) {
        throw std::runtime_error("SQLite connection is not open");
    }
    if (sqlite3_busy_timeout(db, 5000) != SQLITE_OK) {
        throw std::runtime_error("Failed to configure SQLite busy timeout: " +
                                 std::string(sqlite3_errmsg(db)));
    }

    const auto executePragma = [db](const char* sql, const char* setting) {
        char* error = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error ? error : sqlite3_errmsg(db);
            sqlite3_free(error);
            throw std::runtime_error(std::string("Failed to configure SQLite ") +
                                     setting + ": " + message);
        }
    };
    executePragma("PRAGMA foreign_keys=ON;", "foreign keys");
    sqlite3_stmt* foreignKeys = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_keys;", -1, &foreignKeys,
                           nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to verify SQLite foreign keys: " +
                                 std::string(sqlite3_errmsg(db)));
    }
    const bool foreignKeysEnabled =
        sqlite3_step(foreignKeys) == SQLITE_ROW &&
        sqlite3_column_int(foreignKeys, 0) == 1;
    sqlite3_finalize(foreignKeys);
    if (!foreignKeysEnabled) {
        throw std::runtime_error("SQLite foreign keys could not be enabled");
    }
    executePragma("PRAGMA journal_mode=WAL;", "journal mode");
    executePragma("PRAGMA synchronous=NORMAL;", "synchronous mode");
}

inline bool sqliteTableHasColumn(sqlite3* db, const std::string& table,
                                 const std::string& column) {
    std::string quotedTable = "\"";
    for (const char ch : table) {
        quotedTable += ch;
        if (ch == '"') {
            quotedTable += '"';
        }
    }
    quotedTable += '"';

    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "PRAGMA table_info(" + quotedTable + ");";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to inspect SQLite table: " +
                                 std::string(sqlite3_errmsg(db)));
    }

    int result = SQLITE_ROW;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* name =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name && column == name) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    if (result != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to inspect SQLite table: " + error);
    }
    sqlite3_finalize(stmt);
    return false;
}

inline void addSqliteColumnIfMissing(sqlite3* db, const std::string& table,
                                     const std::string& column,
                                     const char* alterSql) {
    if (sqliteTableHasColumn(db, table, column)) {
        return;
    }
    char* error = nullptr;
    if (sqlite3_exec(db, alterSql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("Failed to add SQLite column " + table + "." +
                                 column + ": " + message);
    }
}

} // namespace codepilot

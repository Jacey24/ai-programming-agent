#include "infrastructure/storage/repositories/LogRepository.h"
#include "infrastructure/storage/repositories/SessionRepository.h"
#include "infrastructure/storage/repositories/TaskRepository.h"
#include "infrastructure/storage/SqliteConnection.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace codepilot {

namespace {

void execSql(sqlite3* db, const char* sql) {
    char* error_message = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
        const std::string error = error_message ? error_message : sqlite3_errmsg(db);
        sqlite3_free(error_message);
        throw std::runtime_error(error);
    }
}

} // namespace

class SqliteStorage {
public:
    explicit SqliteStorage(std::string path) : path_(std::move(path)) {}

    void initialize() {
        sqlite3* db = nullptr;
        if (openSqliteConnection(path_.c_str(), &db) != SQLITE_OK) {
            const std::string error = db ? sqlite3_errmsg(db) : "sqlite open failed";
            if (db) {
                sqlite3_close(db);
            }
            throw std::runtime_error(error);
        }
        configureSqliteDatabase(db);

        try {
            execSql(db, R"SQL(
CREATE TABLE IF NOT EXISTS chat_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    prompt TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS workspaces (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    created_at TEXT NOT NULL
);
)SQL");

            SessionRepository(db).initTable();
            TaskRepository(db).initTable();
            LogRepository(db).initTable();
        } catch (...) {
            sqlite3_close(db);
            throw;
        }

        sqlite3_close(db);
    }

private:
    std::string path_;
};

void initializeSqliteStorage(const std::string& path) {
    SqliteStorage(path).initialize();
}

} // namespace codepilot

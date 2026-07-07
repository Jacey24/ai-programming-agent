#include "infrastructure/storage/repositories/LogRepository.h"

#include <stdexcept>

LogRepository::LogRepository(sqlite3* db) : db_(db) {}

void LogRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS execution_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id TEXT NOT NULL,
    type TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL";

    char* error_message = nullptr;
    const int exec_result = sqlite3_exec(db_, sql, nullptr, nullptr, &error_message);
    if (exec_result != SQLITE_OK) {
        const std::string error = error_message ? error_message : lastError();
        sqlite3_free(error_message);
        throw std::runtime_error(error);
    }
}

sqlite3_int64 LogRepository::createLog(
    const std::string& task_id,
    const std::string& type,
    const std::string& content) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO execution_logs (task_id, type, content) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db_);
}

std::vector<LogRecord> LogRepository::findByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, created_at FROM execution_logs WHERE task_id = ? ORDER BY id ASC LIMIT 200;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<LogRecord> logs;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

        logs.push_back(LogRecord{
            sqlite3_column_int64(stmt, 0),
            row_task_id ? row_task_id : "",
            type ? type : "",
            content ? content : "",
            created_at ? created_at : "",
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return logs;
}

std::string LogRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

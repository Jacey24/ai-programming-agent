#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include <stdexcept>

ToolCallRepository::ToolCallRepository(sqlite3* db) : db_(db) {}

void ToolCallRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS tool_calls (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    tool_name TEXT NOT NULL,
    arguments TEXT,
    success INTEGER NOT NULL,
    result TEXT,
    exit_code INTEGER,
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

ToolCallRecord ToolCallRepository::create(
    const std::string& id,
    const std::string& task_id,
    const std::string& tool_name,
    const std::string& arguments,
    bool success,
    const std::string& result,
    int exit_code) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tool_calls (id, task_id, tool_name, arguments, success, result, exit_code) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tool_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, arguments.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, success ? 1 : 0);
    sqlite3_bind_text(stmt, 6, result.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, exit_code);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return ToolCallRecord{id, task_id, tool_name, arguments, success, result, exit_code, ""};
}

std::optional<ToolCallRecord> ToolCallRepository::findById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, tool_name, arguments, success, result, exit_code, created_at FROM tool_calls WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

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

    const auto* row_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const auto* task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const auto* tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* arguments = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const auto* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));

    ToolCallRecord tool_call{
        row_id ? row_id : "",
        task_id ? task_id : "",
        tool_name ? tool_name : "",
        arguments ? arguments : "",
        sqlite3_column_int(stmt, 4) != 0,
        result ? result : "",
        sqlite3_column_int(stmt, 6),
        created_at ? created_at : "",
    };

    sqlite3_finalize(stmt);
    return tool_call;
}

std::vector<ToolCallRecord> ToolCallRepository::findByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, tool_name, arguments, success, result, exit_code, created_at FROM tool_calls WHERE task_id = ? ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<ToolCallRecord> tool_calls;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* arguments = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));

        tool_calls.push_back(ToolCallRecord{
            id ? id : "",
            row_task_id ? row_task_id : "",
            tool_name ? tool_name : "",
            arguments ? arguments : "",
            sqlite3_column_int(stmt, 4) != 0,
            result ? result : "",
            sqlite3_column_int(stmt, 6),
            created_at ? created_at : "",
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return tool_calls;
}

std::string ToolCallRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

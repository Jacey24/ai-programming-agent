#include "infrastructure/storage/repositories/ReplayRepository.h"

#include <stdexcept>

ReplayRepository::ReplayRepository(sqlite3* db) : db_(db) {}

ReplayRecord ReplayRepository::getReplayByTaskId(const std::string& task_id) {
    return ReplayRecord{
        task_id,
        findEventsByTaskId(task_id),
        findToolCallsByTaskId(task_id),
        findPermissionRequestsByTaskId(task_id),
        findFileChangesByTaskId(task_id),
        findExecutionLogsByTaskId(task_id),
    };
}

std::vector<EventRecord> ReplayRepository::findEventsByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at, sequence_no FROM task_events WHERE task_id = ? ORDER BY sequence_no ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<EventRecord> events;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        events.push_back(EventRecord{
            id ? id : "",
            row_task_id ? row_task_id : "",
            type ? type : "",
            content ? content : "",
            metadata ? metadata : "",
            created_at ? created_at : "",
            sqlite3_column_int64(stmt, 6),
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return events;
}

std::vector<ToolCallRecord> ReplayRepository::findToolCallsByTaskId(const std::string& task_id) {
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

std::vector<PermissionRequest> ReplayRepository::findPermissionRequestsByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, tool_name, risk_level, action, reason, status, created_at, resolved_at FROM permission_requests WHERE task_id = ? ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<PermissionRequest> requests;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* risk_level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* action = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        const auto* resolved_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

        requests.push_back(PermissionRequest{
            id ? id : "",
            row_task_id ? row_task_id : "",
            tool_name ? tool_name : "",
            risk_level ? risk_level : "",
            action ? action : "",
            reason ? reason : "",
            status ? status : "",
            created_at ? created_at : "",
            resolved_at ? resolved_at : "",
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return requests;
}

std::vector<FileChangeRecord> ReplayRepository::findFileChangesByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, file_path, change_type, diff, created_at FROM file_changes WHERE task_id = ? ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<FileChangeRecord> file_changes;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* change_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* diff = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        file_changes.push_back(FileChangeRecord{
            id ? id : "",
            row_task_id ? row_task_id : "",
            file_path ? file_path : "",
            change_type ? change_type : "",
            diff ? diff : "",
            created_at ? created_at : "",
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return file_changes;
}

std::vector<LogRecord> ReplayRepository::findExecutionLogsByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, created_at FROM execution_logs WHERE task_id = ? ORDER BY created_at ASC, id ASC;";
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

std::string ReplayRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

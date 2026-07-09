#include "infrastructure/storage/repositories/FileChangeRepository.h"

#include <stdexcept>

FileChangeRepository::FileChangeRepository(sqlite3* db) : db_(db) {}

void FileChangeRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS file_changes (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    file_path TEXT NOT NULL,
    change_type TEXT NOT NULL,
    diff TEXT,
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

FileChangeRecord FileChangeRepository::create(
    const std::string& id,
    const std::string& task_id,
    const std::string& file_path,
    const std::string& change_type,
    const std::string& diff) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO file_changes (id, task_id, file_path, change_type, diff) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, change_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, diff.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return FileChangeRecord{id, task_id, file_path, change_type, diff, ""};
}

std::optional<FileChangeRecord> FileChangeRepository::findById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, file_path, change_type, diff, created_at FROM file_changes WHERE id = ?;";
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
    const auto* file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* change_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const auto* diff = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    FileChangeRecord file_change{
        row_id ? row_id : "",
        task_id ? task_id : "",
        file_path ? file_path : "",
        change_type ? change_type : "",
        diff ? diff : "",
        created_at ? created_at : "",
    };

    sqlite3_finalize(stmt);
    return file_change;
}

std::vector<FileChangeRecord> FileChangeRepository::findByTaskId(const std::string& task_id) {
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

std::vector<FileChangeRecord> FileChangeRepository::findByTaskIdAndPath(
    const std::string& task_id,
    const std::string& file_path) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, file_path, change_type, diff, created_at FROM file_changes WHERE task_id = ? AND file_path = ? ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file_path.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<FileChangeRecord> file_changes;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* row_file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* change_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* diff = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        file_changes.push_back(FileChangeRecord{
            id ? id : "",
            row_task_id ? row_task_id : "",
            row_file_path ? row_file_path : "",
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

std::string FileChangeRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

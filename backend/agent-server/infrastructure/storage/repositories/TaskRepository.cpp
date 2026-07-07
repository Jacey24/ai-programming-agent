#include "infrastructure/storage/repositories/TaskRepository.h"

#include <stdexcept>

TaskRepository::TaskRepository(sqlite3* db) : db_(db) {}

void TaskRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS tasks (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    goal TEXT NOT NULL,
    status TEXT NOT NULL,
    plan TEXT,
    current_step TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
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

TaskRecord TaskRepository::createTask(
    const std::string& id,
    const std::string& session_id,
    const std::string& workspace_id,
    const std::string& goal,
    const std::string& created_at,
    const std::string& updated_at) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tasks (id, session_id, workspace_id, goal, status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, goal.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, "created", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, updated_at.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return TaskRecord{
        id,
        session_id,
        workspace_id,
        goal,
        "created",
        "",
        "",
        created_at,
        updated_at,
    };
}

std::optional<TaskRecord> TaskRepository::findById(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, session_id, workspace_id, goal, status, plan, current_step, created_at, updated_at FROM tasks WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);

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

    const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const auto* session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const auto* workspace_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* goal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const auto* plan = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    const auto* current_step = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    const auto* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

    TaskRecord task{
        id ? id : "",
        session_id ? session_id : "",
        workspace_id ? workspace_id : "",
        goal ? goal : "",
        status ? status : "",
        plan ? plan : "",
        current_step ? current_step : "",
        created_at ? created_at : "",
        updated_at ? updated_at : "",
    };

    sqlite3_finalize(stmt);
    return task;
}

std::string TaskRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

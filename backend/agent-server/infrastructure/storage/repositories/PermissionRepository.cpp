#include "infrastructure/storage/repositories/PermissionRepository.h"

#include <stdexcept>

PermissionRepository::PermissionRepository(sqlite3* db) : db_(db) {}

void PermissionRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS permission_requests (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    tool_name TEXT NOT NULL,
    risk_level TEXT NOT NULL,
    action TEXT NOT NULL,
    reason TEXT,
    status TEXT NOT NULL DEFAULT 'pending',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved_at TEXT
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

std::optional<PermissionRequest> PermissionRepository::findById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, tool_name, risk_level, action, reason, status, created_at, resolved_at FROM permission_requests WHERE id = ?;";
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
    const auto* risk_level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const auto* action = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const auto* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    const auto* resolved_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

    PermissionRequest request{
        row_id ? row_id : "",
        task_id ? task_id : "",
        tool_name ? tool_name : "",
        risk_level ? risk_level : "",
        action ? action : "",
        reason ? reason : "",
        status ? status : "",
        created_at ? created_at : "",
        resolved_at ? resolved_at : "",
    };

    sqlite3_finalize(stmt);
    return request;
}

std::vector<PermissionRequest> PermissionRepository::findPending() {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, tool_name, risk_level, action, reason, status, created_at, resolved_at FROM permission_requests WHERE status = 'pending' ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    std::vector<PermissionRequest> requests;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* risk_level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* action = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        const auto* resolved_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

        requests.push_back(PermissionRequest{
            id ? id : "",
            task_id ? task_id : "",
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

void PermissionRepository::updateStatus(const std::string& id, const std::string& status) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE permission_requests SET status = ?, resolved_at = CURRENT_TIMESTAMP WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
}

std::string PermissionRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

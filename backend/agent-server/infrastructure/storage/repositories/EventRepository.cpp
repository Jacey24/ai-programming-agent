#include "infrastructure/storage/repositories/EventRepository.h"

#include <stdexcept>

EventRepository::EventRepository(sqlite3* db) : db_(db) {}

void EventRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS task_events (
    id TEXT PRIMARY KEY,
    task_id TEXT NOT NULL,
    type TEXT NOT NULL,
    content TEXT,
    metadata TEXT,
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

EventRecord EventRepository::create(
    const std::string& id,
    const std::string& task_id,
    const std::string& type,
    const std::string& content,
    const std::string& metadata) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO task_events (id, task_id, type, content, metadata) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metadata.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return EventRecord{id, task_id, type, content, metadata, ""};
}

std::optional<EventRecord> EventRepository::findById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at FROM task_events WHERE id = ?;";
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
    const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const auto* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    EventRecord event{
        row_id ? row_id : "",
        task_id ? task_id : "",
        type ? type : "",
        content ? content : "",
        metadata ? metadata : "",
        created_at ? created_at : "",
    };

    sqlite3_finalize(stmt);
    return event;
}

std::vector<EventRecord> EventRepository::findByTaskId(const std::string& task_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at FROM task_events WHERE task_id = ? ORDER BY created_at ASC;";
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

std::vector<EventRecord> EventRepository::findByTaskIdAndType(const std::string& task_id, const std::string& type) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at FROM task_events WHERE task_id = ? AND type = ? ORDER BY created_at ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<EventRecord> events;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* row_task_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* row_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const auto* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        events.push_back(EventRecord{
            id ? id : "",
            row_task_id ? row_task_id : "",
            row_type ? row_type : "",
            content ? content : "",
            metadata ? metadata : "",
            created_at ? created_at : "",
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

std::string EventRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

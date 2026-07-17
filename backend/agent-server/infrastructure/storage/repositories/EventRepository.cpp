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
    char* transaction_error = nullptr;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr,
                     &transaction_error) != SQLITE_OK) {
        const std::string error = transaction_error ? transaction_error : lastError();
        sqlite3_free(transaction_error);
        throw std::runtime_error(error);
    }

    sqlite3_stmt* sequence_stmt = nullptr;
    const char* sequence_sql =
        "SELECT COALESCE(MAX(sequence_no), 0) + 1 FROM task_events WHERE task_id = ?;";
    if (sqlite3_prepare_v2(db_, sequence_sql, -1, &sequence_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(lastError());
    }
    sqlite3_bind_text(sequence_stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(sequence_stmt) != SQLITE_ROW) {
        const std::string error = lastError();
        sqlite3_finalize(sequence_stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(error);
    }
    const std::int64_t sequence_no = sqlite3_column_int64(sequence_stmt, 0);
    sqlite3_finalize(sequence_stmt);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO task_events (id, task_id, type, content, metadata, sequence_no) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metadata.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, sequence_no);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &transaction_error) != SQLITE_OK) {
        const std::string error = transaction_error ? transaction_error : lastError();
        sqlite3_free(transaction_error);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(error);
    }
    return EventRecord{id, task_id, type, content, metadata, "", sequence_no};
}

std::optional<EventRecord> EventRepository::findById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at, sequence_no FROM task_events WHERE id = ?;";
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
        sqlite3_column_int64(stmt, 6),
    };

    sqlite3_finalize(stmt);
    return event;
}

std::vector<EventRecord> EventRepository::findByTaskId(const std::string& task_id) {
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

std::vector<EventRecord> EventRepository::findByTaskIdAfterSequence(
    const std::string& task_id, std::int64_t after_sequence) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, task_id, type, content, metadata, created_at, sequence_no "
        "FROM task_events WHERE task_id = ? AND sequence_no > ? "
        "ORDER BY sequence_no ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }
    sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, after_sequence);
    std::vector<EventRecord> events;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto text = [stmt](int column) {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
            return std::string(value ? value : "");
        };
        events.push_back(EventRecord{text(0), text(1), text(2), text(3),
                                     text(4), text(5), sqlite3_column_int64(stmt, 6)});
    }
    if (result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }
    sqlite3_finalize(stmt);
    return events;
}

std::vector<EventRecord> EventRepository::findByTaskIdAndType(const std::string& task_id, const std::string& type) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, task_id, type, content, metadata, created_at, sequence_no FROM task_events WHERE task_id = ? AND type = ? ORDER BY sequence_no ASC;";
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

std::string EventRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

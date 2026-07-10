#include "infrastructure/storage/repositories/SessionRepository.h"

#include <stdexcept>

SessionRepository::SessionRepository(sqlite3* db) : db_(db) {}

void SessionRepository::initTable() {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
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

SessionRecord SessionRepository::createSession(
    const std::string& id,
    const std::string& title,
    const std::string& created_at,
    const std::string& updated_at) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO sessions (id, title, created_at, updated_at) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, updated_at.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return SessionRecord{id, title, created_at, updated_at};
}

std::optional<SessionRecord> SessionRepository::findById(const std::string& session_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, created_at, updated_at FROM sessions WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

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
    const auto* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    SessionRecord session{
        id ? id : "",
        title ? title : "",
        created_at ? created_at : "",
        updated_at ? updated_at : "",
    };

    sqlite3_finalize(stmt);
    return session;
}

std::string SessionRepository::lastError() const {
    return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}

std::vector<SessionRecord> SessionRepository::listAll() {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, title, created_at, updated_at FROM sessions ORDER BY created_at DESC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    std::vector<SessionRecord> sessions;
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

        sessions.push_back(SessionRecord{
            id ? id : "",
            title ? title : "",
            created_at ? created_at : "",
            updated_at ? updated_at : "",
        });
    }

    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return sessions;
}

SessionRecord SessionRepository::updateTitle(
    const std::string& id,
    const std::string& title,
    const std::string& updated_at) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE sessions SET title = ?, updated_at = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, id.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    const int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (changes == 0) {
        throw std::runtime_error("session not found");
    }

    return findById(id).value();
}

bool SessionRepository::deleteById(const std::string& id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM sessions WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(lastError());
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_DONE) {
        const std::string error = lastError();
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    const int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changes > 0;
}

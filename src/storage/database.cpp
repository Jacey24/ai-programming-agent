#include "database.h"
#include <sqlite3.h>

namespace storage {

static sqlite3* db_ = nullptr;

bool Database::open(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) return false;

    // Create tables
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id TEXT PRIMARY KEY,
            title TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id TEXT,
            role TEXT,
            content TEXT,
            tool_calls TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS tasks (
            id TEXT PRIMARY KEY,
            conversation_id TEXT,
            description TEXT,
            status TEXT DEFAULT 'pending',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS execution_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            task_id TEXT,
            tool_name TEXT,
            params TEXT,
            result TEXT,
            duration_ms INTEGER,
            success INTEGER,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    return true;
}

void Database::close() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

std::string Database::create_conversation(const std::string& title) {
    // TODO: Generate UUID, insert row
    return "conv-001";
}

std::vector<json> Database::list_conversations() {
    // TODO: SELECT all conversations
    return {};
}

void Database::save_message(const std::string& conv_id, const std::string& role,
                             const std::string& content, const json& tool_calls) {
    // TODO: INSERT into messages
}

void Database::log_execution(const std::string& task_id, const std::string& tool,
                              const json& params, const std::string& result,
                              int duration_ms, bool success) {
    // TODO: INSERT into execution_logs
}

} // namespace storage

#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct SessionRecord {
    std::string id;
    std::string title;
    std::string created_at;
    std::string updated_at;
};

class SessionRepository {
public:
    explicit SessionRepository(sqlite3* db);

    void initTable();
    SessionRecord createSession(
        const std::string& id,
        const std::string& title,
        const std::string& created_at,
        const std::string& updated_at);
    std::optional<SessionRecord> findById(const std::string& session_id);
    std::vector<SessionRecord> listAll();

private:
    sqlite3* db_;

    std::string lastError() const;
};

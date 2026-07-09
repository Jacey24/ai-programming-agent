#pragma once

#include "infrastructure/storage/repositories/SessionRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

class SessionService {
public:
    explicit SessionService(sqlite3* db);

    SessionRecord createSession(const std::string& title);
    std::vector<SessionRecord> listSessions();
    std::optional<SessionRecord> getSessionById(const std::string& session_id);

private:
    sqlite3* db_;
};

} // namespace codepilot

#pragma once

#include "infrastructure/storage/repositories/SessionRepository.h"

#include <string>

namespace codepilot {

class SessionService {
public:
    explicit SessionService(sqlite3* db);

    SessionRecord createSession(const std::string& title);

private:
    sqlite3* db_;
};

} // namespace codepilot

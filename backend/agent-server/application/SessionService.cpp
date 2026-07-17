#include "application/SessionService.h"

#include <chrono>
#include <ctime>

namespace codepilot {

namespace {

std::string currentTimestamp() {
    const auto now = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

std::string generateId(const std::string& prefix) {
    return prefix + "_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

SessionService::SessionService(sqlite3* db) : db_(db) {}

SessionRecord SessionService::createSession(const std::string& title,
                                            const std::string& workspace_id) {
    const std::string now = currentTimestamp();
    SessionRepository repository(db_);
    return repository.createSession(generateId("session"), title, workspace_id,
                                    now, now);
}

std::vector<SessionRecord> SessionService::listSessions() {
    return SessionRepository(db_).listAll();
}

std::optional<SessionRecord> SessionService::getSessionById(const std::string& session_id) {
    return SessionRepository(db_).findById(session_id);
}

} // namespace codepilot

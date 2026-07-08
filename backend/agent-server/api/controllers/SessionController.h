#pragma once

#include <string>

namespace codepilot {

class SessionController {
public:
    explicit SessionController(std::string database_path = "/data/agent.db");

    std::string createSession(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

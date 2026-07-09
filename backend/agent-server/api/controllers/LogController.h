#pragma once

#include <string>

namespace codepilot {

class LogController {
public:
    explicit LogController(std::string database_path = "/data/agent.db");

    std::string listLogs(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

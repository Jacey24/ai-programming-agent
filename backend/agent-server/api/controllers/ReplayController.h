#pragma once

#include <string>

namespace codepilot {

class ReplayController {
public:
    explicit ReplayController(std::string database_path = "/data/agent.db");

    std::string getReplay(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

#pragma once

#include <string>

namespace codepilot {

class FileChangeController {
public:
    explicit FileChangeController(std::string database_path = "/data/agent.db");

    std::string listFileChanges(const std::string& request);
    std::string getFileChange(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

#pragma once

#include <string>

namespace codepilot {

class WorkspaceFileController {
public:
    explicit WorkspaceFileController(std::string database_path = "/data/agent.db");

    std::string getTree(const std::string& request);
    std::string getFileContent(const std::string& request);
    std::string saveFileContent(const std::string& request);
    std::string revealInFileManager(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

#pragma once

#include <string>

namespace codepilot {

class WorkspaceController {
public:
    explicit WorkspaceController(std::string database_path = "/data/agent.db");

    std::string createWorkspace(const std::string& request);
    std::string listWorkspaces(const std::string& request);
    std::string getWorkspace(const std::string& request);

    // 工作区文件操作
    std::string listFiles(const std::string& request);
    std::string getFileContent(const std::string& request);

    // 工作区路径校验
    std::string validateWorkspace(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

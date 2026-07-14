#pragma once

#include <string>

namespace codepilot {

class WorkspaceController {
public:
  explicit WorkspaceController(std::string database_path = "/data/agent.db");

  std::string selectLocalDirectory(const std::string &request);
  std::string createWorkspace(const std::string &request);
  std::string updateWorkspace(const std::string &request);
  std::string listWorkspaces(const std::string &request);
  std::string getWorkspace(const std::string &request);
  std::string deleteWorkspace(const std::string &request);

private:
  std::string databasePath_;
};

} // namespace codepilot

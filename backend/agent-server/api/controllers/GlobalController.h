#pragma once

#include <string>

namespace codepilot {

class GlobalController {
public:
  explicit GlobalController(std::string database_path = "/data/agent.db");

  std::string createGlobal(const std::string &request);
  std::string getGlobal(const std::string &request);
  std::string listGlobals(const std::string &request);
  std::string getGlobalContext(const std::string &request);

private:
  std::string databasePath_;
};

} // namespace codepilot
#pragma once

#include <string>

namespace codepilot {

class TaskController {
public:
  explicit TaskController(std::string database_path = "/data/agent.db");

  std::string createTask(const std::string &request);
  std::string continueTask(const std::string &request);
  std::string listTasks(const std::string &request);
  std::string getTask(const std::string &request);
  std::string cancelTask(const std::string &request);
  std::string deleteTask(const std::string &request);
  std::string listToolCalls(const std::string &request);
  std::string listEventHistory(const std::string &request);
  std::string listActiveTasks();

private:
  std::string databasePath_;
};

} // namespace codepilot
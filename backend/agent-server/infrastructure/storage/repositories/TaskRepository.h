#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct TaskRecord {
  std::string id;
  std::string session_id;
  std::string workspace_id;
  std::string goal;
  std::string status;
  std::string plan;
  std::string current_step;
  std::string created_at;
  std::string updated_at;
};

class TaskRepository {
public:
  explicit TaskRepository(sqlite3 *db);

  void initTable();
  TaskRecord createTask(const std::string &id, const std::string &session_id,
                        const std::string &workspace_id,
                        const std::string &goal, const std::string &created_at,
                        const std::string &updated_at);
  std::optional<TaskRecord> findById(const std::string &task_id);
  std::vector<TaskRecord> listRecent(int limit);
  std::vector<TaskRecord> findBySessionId(const std::string &session_id);
  void updateExecution(const std::string &task_id, const std::string &status,
                       const std::string &plan, const std::string &current_step,
                       const std::string &updated_at);

private:
  sqlite3 *db_;

  std::string lastError() const;
};
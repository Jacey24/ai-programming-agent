#pragma once

#include "infrastructure/storage/repositories/TaskRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

class TaskService {
public:
    explicit TaskService(sqlite3* db);

    TaskRecord createTask(
        const std::string& session_id,
        const std::string& workspace_id,
        const std::string& goal);
    std::optional<TaskRecord> getTask(const std::string& task_id);
    std::vector<TaskRecord> listTasks(int limit);
    void updateExecution(
        const std::string& task_id,
        const std::string& status,
        const std::string& plan,
        const std::string& current_step);

private:
    sqlite3* db_;
};

} // namespace codepilot

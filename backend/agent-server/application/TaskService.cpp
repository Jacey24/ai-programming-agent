#include "application/TaskService.h"

#include <chrono>
#include <ctime>

namespace codepilot {

namespace {

std::string currentTimestamp() {
    const auto now = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

std::string generateId(const std::string& prefix) {
    return prefix + "_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TaskService::TaskService(sqlite3* db) : db_(db) {}

TaskRecord TaskService::createTask(
    const std::string& session_id,
    const std::string& workspace_id,
    const std::string& goal) {
    const std::string now = currentTimestamp();
    TaskRepository repository(db_);
    return repository.createTask(generateId("task"), session_id, workspace_id, goal, now, now);
}

std::optional<TaskRecord> TaskService::getTask(const std::string& task_id) {
    TaskRepository repository(db_);
    return repository.findById(task_id);
}

std::vector<TaskRecord> TaskService::listTasks(int limit) {
    TaskRepository repository(db_);
    return repository.listRecent(limit);
}

void TaskService::updateExecution(
    const std::string& task_id,
    const std::string& status,
    const std::string& plan,
    const std::string& current_step) {
    TaskRepository repository(db_);
    repository.updateExecution(task_id, status, plan, current_step, currentTimestamp());
}

} // namespace codepilot

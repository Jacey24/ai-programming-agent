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

TaskRecord TaskService::cancelTask(const std::string& task_id) {
    auto task = getTask(task_id);
    if (!task) {
        throw std::runtime_error("task not found");
    }
    updateExecution(task_id, "cancelled", task->plan, task->current_step);
    return getTask(task_id).value();
}

std::vector<ToolCallRecord> TaskService::getToolCalls(const std::string& task_id) {
    // 先确认 task 存在
    auto task = getTask(task_id);
    if (!task) {
        throw std::runtime_error("task not found");
    }
    ToolCallRepository repo(db_);
    return repo.findByTaskId(task_id);
}

std::vector<EventData> TaskService::getEventHistory(const std::string& task_id,
                                                    EventBus& eventBus) {
    auto task = getTask(task_id);
    if (!task) {
        throw std::runtime_error("task not found");
    }
    return eventBus.getHistory(task_id);
}

TaskRecord TaskService::retryTask(const std::string& task_id) {
    const auto old = getTask(task_id);
    if (!old) {
        throw std::runtime_error("task not found");
    }
    return createTask(old->session_id, old->workspace_id, old->goal);
}

} // namespace codepilot

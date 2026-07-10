#pragma once

#include "event/EventBus.h"
#include "infrastructure/storage/repositories/TaskRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

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

    TaskRecord cancelTask(const std::string& task_id);

    // 工具调用记录
    std::vector<ToolCallRecord> getToolCalls(const std::string& task_id);

    // 事件历史（需外部 EventBus 实例）
    std::vector<EventData> getEventHistory(const std::string& task_id,
                                           EventBus& eventBus);

    // 重试任务（创建新任务，复用原 session/workspace/goal）
    TaskRecord retryTask(const std::string& task_id);

private:
    sqlite3* db_;
};

} // namespace codepilot

#pragma once

#include <atomic>
#include <string>

namespace codepilot {

class EventDispatcher;

class TaskController {
public:
    explicit TaskController(std::string database_path = "/data/agent.db");

    std::string createTask(const std::string& request);
    std::string listTasks(const std::string& request);
    std::string getTask(const std::string& request);
    std::string cancelTask(const std::string& request);

    // SSE 事件流处理（长连接，直接操作 socket）
    // 通过 EventDispatcher.registerClient() 管理 SSE 客户端
    void handleEvents(int client_fd,
                      const std::string& task_id,
                      EventDispatcher& dispatcher,
                      const std::atomic_bool& running);

private:
    std::string databasePath_;
};

} // namespace codepilot

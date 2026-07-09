#pragma once

#include "infrastructure/storage/repositories/EventRepository.h"
#include "infrastructure/storage/repositories/FileChangeRepository.h"
#include "infrastructure/storage/repositories/LogRepository.h"
#include "infrastructure/storage/repositories/PermissionRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include <sqlite3.h>

#include <string>
#include <vector>

struct ReplayRecord {
    std::string task_id;
    std::vector<EventRecord> events;
    std::vector<ToolCallRecord> tool_calls;
    std::vector<PermissionRequest> permission_requests;
    std::vector<FileChangeRecord> file_changes;
    std::vector<LogRecord> execution_logs;
};

class ReplayRepository {
public:
    explicit ReplayRepository(sqlite3* db);

    ReplayRecord getReplayByTaskId(const std::string& task_id);

private:
    sqlite3* db_;

    std::vector<EventRecord> findEventsByTaskId(const std::string& task_id);
    std::vector<ToolCallRecord> findToolCallsByTaskId(const std::string& task_id);
    std::vector<PermissionRequest> findPermissionRequestsByTaskId(const std::string& task_id);
    std::vector<FileChangeRecord> findFileChangesByTaskId(const std::string& task_id);
    std::vector<LogRecord> findExecutionLogsByTaskId(const std::string& task_id);

    std::string lastError() const;
};

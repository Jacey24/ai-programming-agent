#pragma once

#include "infrastructure/storage/repositories/TaskRepository.h"

#include <sqlite3.h>

#include <string>
#include <vector>

namespace codepilot {

struct ReplayTimelineItem {
    std::string type;
    std::string tool_name;
    std::string file_path;
    std::string content;
    std::string created_at;
};

struct ReplayResult {
    TaskRecord task;
    std::vector<ReplayTimelineItem> timeline;
};

class ReplayService {
public:
    explicit ReplayService(sqlite3* db);

    ReplayResult buildReplay(const std::string& task_id);

private:
    sqlite3* db_;
};

} // namespace codepilot

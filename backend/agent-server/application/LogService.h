#pragma once

#include "infrastructure/storage/repositories/LogRepository.h"

#include <string>
#include <vector>

namespace codepilot {

class LogService {
public:
    explicit LogService(sqlite3* db);

    std::vector<LogRecord> getLogsByTaskId(const std::string& task_id);
    sqlite3_int64 createLog(
        const std::string& task_id,
        const std::string& type,
        const std::string& content);

private:
    sqlite3* db_;
};

} // namespace codepilot

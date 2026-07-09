#include "application/LogService.h"

namespace codepilot {

LogService::LogService(sqlite3* db) : db_(db) {
    LogRepository(db_).initTable();
}

std::vector<LogRecord> LogService::getLogsByTaskId(const std::string& task_id) {
    return LogRepository(db_).findByTaskId(task_id);
}

sqlite3_int64 LogService::createLog(
    const std::string& task_id,
    const std::string& type,
    const std::string& content) {
    return LogRepository(db_).createLog(task_id, type, content);
}

} // namespace codepilot

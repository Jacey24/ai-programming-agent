#include "application/FileChangeService.h"

namespace codepilot {

FileChangeService::FileChangeService(sqlite3* db) : db_(db) {
    FileChangeRepository(db_).initTable();
}

std::vector<FileChangeRecord> FileChangeService::getFileChangesByTaskId(const std::string& task_id) {
    return FileChangeRepository(db_).findByTaskId(task_id);
}

std::optional<FileChangeRecord> FileChangeService::getFileChangeById(const std::string& id) {
    return FileChangeRepository(db_).findById(id);
}

} // namespace codepilot

#pragma once

#include "infrastructure/storage/repositories/FileChangeRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

class FileChangeService {
public:
    explicit FileChangeService(sqlite3* db);

    std::vector<FileChangeRecord> getFileChangesByTaskId(const std::string& task_id);
    std::optional<FileChangeRecord> getFileChangeById(const std::string& id);

private:
    sqlite3* db_;
};

} // namespace codepilot

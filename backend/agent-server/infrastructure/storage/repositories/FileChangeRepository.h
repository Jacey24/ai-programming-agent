#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct FileChangeRecord {
    std::string id;
    std::string task_id;
    std::string file_path;
    std::string change_type;
    std::string diff;
    std::string created_at;
};

class FileChangeRepository {
public:
    explicit FileChangeRepository(sqlite3* db);

    void initTable();
    FileChangeRecord create(
        const std::string& id,
        const std::string& task_id,
        const std::string& file_path,
        const std::string& change_type,
        const std::string& diff);
    std::optional<FileChangeRecord> findById(const std::string& id);
    std::vector<FileChangeRecord> findByTaskId(const std::string& task_id);
    std::vector<FileChangeRecord> findByTaskIdAndPath(const std::string& task_id, const std::string& file_path);

private:
    sqlite3* db_;

    std::string lastError() const;
};

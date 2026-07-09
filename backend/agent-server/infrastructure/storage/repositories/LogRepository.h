#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct LogRecord {
    sqlite3_int64 id{0};
    std::string task_id;
    std::string type;
    std::string content;
    std::string created_at;
};

class LogRepository {
public:
    explicit LogRepository(sqlite3* db);

    void initTable();
    sqlite3_int64 createLog(const std::string& task_id, const std::string& type, const std::string& content);
    std::optional<LogRecord> findById(sqlite3_int64 id);
    std::vector<LogRecord> findByTaskId(const std::string& task_id);
    sqlite3_int64 countByTaskId(const std::string& task_id);

private:
    sqlite3* db_;

    std::string lastError() const;
};

#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct ToolCallRecord {
    std::string id;
    std::string task_id;
    std::string tool_name;
    std::string arguments;
    bool success{false};
    std::string result;
    int exit_code{0};
    std::string created_at;
};

class ToolCallRepository {
public:
    explicit ToolCallRepository(sqlite3* db);

    void initTable();
    ToolCallRecord create(
        const std::string& id,
        const std::string& task_id,
        const std::string& tool_name,
        const std::string& arguments,
        bool success,
        const std::string& result,
        int exit_code);
    std::optional<ToolCallRecord> findById(const std::string& id);
    std::vector<ToolCallRecord> findByTaskId(const std::string& task_id);

private:
    sqlite3* db_;

    std::string lastError() const;
};

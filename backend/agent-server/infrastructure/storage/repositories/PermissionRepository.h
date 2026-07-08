#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct PermissionRequest {
    std::string id;
    std::string task_id;
    std::string tool_name;
    std::string risk_level;
    std::string action;
    std::string reason;
    std::string status;
    std::string created_at;
    std::string resolved_at;
};

class PermissionRepository {
public:
    explicit PermissionRepository(sqlite3* db);

    void initTable();

    std::optional<PermissionRequest> findById(const std::string& id);
    std::vector<PermissionRequest> findPending();
    void updateStatus(const std::string& id, const std::string& status);

private:
    sqlite3* db_;

    std::string lastError() const;
};

#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct WorkspaceRecord {
    std::string id;
    std::string name;
    std::string path;
    std::string created_at;
};

class WorkspaceRepository {
public:
    explicit WorkspaceRepository(sqlite3* db);

    void initTable();
    std::optional<WorkspaceRecord> findById(const std::string& workspace_id);
    std::vector<WorkspaceRecord> listAll();

private:
    sqlite3* db_;

    std::string lastError() const;
};

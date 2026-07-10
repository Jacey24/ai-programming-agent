#pragma once

#include "infrastructure/storage/repositories/WorkspaceRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

class WorkspaceService {
public:
    explicit WorkspaceService(sqlite3* db);

    std::vector<WorkspaceRecord> listWorkspaces();
    std::optional<WorkspaceRecord> getWorkspaceById(const std::string& workspace_id);

private:
    sqlite3* db_;
};

} // namespace codepilot

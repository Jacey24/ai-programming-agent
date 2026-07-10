#include "application/WorkspaceService.h"

namespace codepilot {

WorkspaceService::WorkspaceService(sqlite3* db) : db_(db) {}

std::vector<WorkspaceRecord> WorkspaceService::listWorkspaces() {
    return WorkspaceRepository(db_).listAll();
}

std::optional<WorkspaceRecord> WorkspaceService::getWorkspaceById(const std::string& workspace_id) {
    return WorkspaceRepository(db_).findById(workspace_id);
}

} // namespace codepilot

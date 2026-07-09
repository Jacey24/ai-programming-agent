#include "application/PermissionService.h"

namespace codepilot {

PermissionService::PermissionService(sqlite3* db) : db_(db) {
    PermissionRepository(db_).initTable();
}

std::vector<PermissionRequest> PermissionService::listPendingRequests(const std::string& task_id) {
    auto all = PermissionRepository(db_).findPending();
    if (task_id.empty()) {
        return all;
    }
    std::vector<PermissionRequest> filtered;
    for (const auto& req : all) {
        if (req.task_id == task_id) {
            filtered.push_back(req);
        }
    }
    return filtered;
}

std::optional<PermissionRequest> PermissionService::getRequest(const std::string& id) {
    return PermissionRepository(db_).findById(id);
}

bool PermissionService::approveRequest(const std::string& id) {
    auto req = PermissionRepository(db_).findById(id);
    if (!req || req->status != "pending") {
        return false;
    }
    PermissionRepository(db_).updateStatus(id, "approved");
    return true;
}

bool PermissionService::rejectRequest(const std::string& id) {
    auto req = PermissionRepository(db_).findById(id);
    if (!req || req->status != "pending") {
        return false;
    }
    PermissionRepository(db_).updateStatus(id, "rejected");
    return true;
}

} // namespace codepilot

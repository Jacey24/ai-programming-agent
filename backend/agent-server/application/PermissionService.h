#pragma once

#include "infrastructure/storage/repositories/PermissionRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

class PermissionService {
public:
    explicit PermissionService(sqlite3* db);

    std::vector<PermissionRequest> listPendingRequests(const std::string& task_id = "");
    std::optional<PermissionRequest> getRequest(const std::string& id);
    bool approveRequest(const std::string& id);
    bool rejectRequest(const std::string& id);

private:
    sqlite3* db_;
};

} // namespace codepilot

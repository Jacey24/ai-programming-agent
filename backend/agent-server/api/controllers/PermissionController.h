#pragma once

#include <string>

namespace codepilot {

class PermissionController {
public:
    explicit PermissionController(std::string database_path = "/data/agent.db");

    std::string listPermissions(const std::string& request);
    std::string getPermission(const std::string& request);
    std::string handleAction(const std::string& request);

private:
    std::string databasePath_;
};

} // namespace codepilot

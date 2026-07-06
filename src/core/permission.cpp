#include "permission.h"

namespace core {

bool PermissionController::check(const std::string& tool, const std::string& params) {
    // TODO: Risk-based permission check
    return true;
}

void PermissionController::grant(const std::string& tool) {
    allowed_.insert(tool);
}

void PermissionController::revoke(const std::string& tool) {
    allowed_.erase(tool);
}

} // namespace core

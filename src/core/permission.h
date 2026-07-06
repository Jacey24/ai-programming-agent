#pragma once

#include <string>
#include <set>

namespace core {

enum class RiskLevel { Safe, NeedsConfirm, Dangerous };

class PermissionController {
public:
    bool check(const std::string& tool, const std::string& params);
    void grant(const std::string& tool);
    void revoke(const std::string& tool);

private:
    std::set<std::string> allowed_;
};

} // namespace core

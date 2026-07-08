#pragma once

#include <string>
#include <vector>

namespace codepilot {

struct RoleConfig {
    std::string name;
    std::string description;
    std::string promptTemplate;
    std::vector<std::string> visibleTools;
    std::string outputFormat;
};

class RoleRegistry {
public:
    bool loadFromFile(const std::string& configPath);
    const RoleConfig* findByName(const std::string& name) const;
    size_t count() const { return roles_.size(); }

private:
    std::vector<RoleConfig> roles_;
};

} // namespace codepilot
#pragma once

#include <string>

namespace codepilot {

class ToolController {
public:
    std::string listTools() const;
    std::string getToolDetail(const std::string& request) const;
};

} // namespace codepilot

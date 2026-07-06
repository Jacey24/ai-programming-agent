#pragma once

#include <string>

namespace codepilot {

enum class RiskLevel {
    Safe,
    Medium,
    Dangerous,
    Blocked
};

struct ToolResult {
    bool success{false};
    std::string output;
    std::string error;
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
};

} // namespace codepilot

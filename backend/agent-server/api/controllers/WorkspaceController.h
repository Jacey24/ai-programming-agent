#pragma once

#include <string>

namespace codepilot {

class WorkspaceController {
public:
    std::string createWorkspace(const std::string& request);
};

} // namespace codepilot

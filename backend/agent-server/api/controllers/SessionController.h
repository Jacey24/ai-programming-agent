#pragma once

#include <string>

namespace codepilot {

class SessionController {
public:
    std::string createSession(const std::string& request);
};

} // namespace codepilot

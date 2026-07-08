#pragma once

#include <string>

namespace codepilot {

class TaskController {
public:
    std::string createTask(const std::string& request);
    std::string getTask(const std::string& request);
};

} // namespace codepilot

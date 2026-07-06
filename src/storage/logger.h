#pragma once

#include <string>

namespace storage {

class Logger {
public:
    static void init(const std::string& log_path);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void tool_call(const std::string& tool, const std::string& params,
                          const std::string& result, int duration_ms);
};

} // namespace storage

#include "logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace storage {

void Logger::init(const std::string& log_path) {
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, true);
    auto logger = std::make_shared<spdlog::logger>("agent", file_sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
}

void Logger::info(const std::string& msg)  { spdlog::info(msg); }
void Logger::warn(const std::string& msg)  { spdlog::warn(msg); }
void Logger::error(const std::string& msg) { spdlog::error(msg); }

void Logger::tool_call(const std::string& tool, const std::string& params,
                        const std::string& result, int duration_ms) {
    spdlog::info("[tool:{}] params={} result={} duration={}ms", tool, params, result, duration_ms);
}

} // namespace storage

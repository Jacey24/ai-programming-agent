#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace storage {

class Database {
public:
    bool open(const std::string& path);
    void close();

    // Conversations
    std::string create_conversation(const std::string& title);
    std::vector<json> list_conversations();

    // Messages
    void save_message(const std::string& conv_id, const std::string& role,
                      const std::string& content, const json& tool_calls = {});

    // Execution logs
    void log_execution(const std::string& task_id, const std::string& tool,
                       const json& params, const std::string& result,
                       int duration_ms, bool success);
};

} // namespace storage

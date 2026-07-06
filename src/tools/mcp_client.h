#pragma once

#include <string>
#include <vector>
#include "itool.h"

struct McpServerConfig {
    std::string name;
    std::string command;   // e.g. "npx", "python"
    std::vector<std::string> args;
};

class McpClient {
public:
    void connect(const McpServerConfig& config);
    std::vector<ToolResult> list_tools();
    ToolResult call_tool(const std::string& name, const json& params);

private:
    // TODO: JSON-RPC 2.0 over stdio/SSE
};

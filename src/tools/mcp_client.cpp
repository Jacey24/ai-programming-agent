#include "mcp_client.h"

void McpClient::connect(const McpServerConfig& config) {
    // TODO: Spawn MCP server process, establish JSON-RPC stdio connection
}

std::vector<ToolResult> McpClient::list_tools() {
    // TODO: Send tools/list request
    return {};
}

ToolResult McpClient::call_tool(const std::string& name, const json& params) {
    // TODO: Send tools/call request
    return {true, "MCP tool result placeholder"};
}

#include "web_tools.h"

// WebSearchTool
std::string WebSearchTool::name() const { return "web_search"; }
std::string WebSearchTool::description() const { return "Search the web"; }

json WebSearchTool::parameters_schema() const {
    return {{"query", {{"type", "string"}, {"description", "Search query"}}}};
}

ToolResult WebSearchTool::execute(const json& params) {
    // TODO: HTTP search API call
    return {true, "Search results placeholder"};
}

// WebFetchTool
std::string WebFetchTool::name() const { return "web_fetch"; }
std::string WebFetchTool::description() const { return "Fetch a URL and convert to markdown"; }

json WebFetchTool::parameters_schema() const {
    return {
        {"url", {{"type", "string"}, {"description", "URL to fetch"}}},
        {"prompt", {{"type", "string"}, {"description", "Question about the page"}}}
    };
}

ToolResult WebFetchTool::execute(const json& params) {
    // TODO: HTTP GET + HTML→Markdown conversion
    return {true, "Fetched content placeholder"};
}

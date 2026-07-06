#include "registry.h"

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    tools_[tool->name()] = std::move(tool);
}

ToolResult ToolRegistry::call(const std::string& name, const json& params) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return {false, "Tool not found: " + name};
    }
    return it->second->execute(params);
}

json ToolRegistry::generate_tools_schema() const {
    // TODO: Generate OpenAI function-calling format from registered tools
    return json::array();
}

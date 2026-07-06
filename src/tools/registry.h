#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "itool.h"

class ToolRegistry {
public:
    void register_tool(std::unique_ptr<ITool> tool);
    ToolResult call(const std::string& name, const json& params);
    json generate_tools_schema() const;

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
};

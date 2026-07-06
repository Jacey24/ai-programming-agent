#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ToolResult {
    bool success;
    std::string content;
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual ToolResult execute(const json& params) = 0;
    virtual bool requires_approval() const { return false; }
};

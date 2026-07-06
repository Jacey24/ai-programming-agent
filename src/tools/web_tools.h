#pragma once

#include "itool.h"

class WebSearchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
};

class WebFetchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
};

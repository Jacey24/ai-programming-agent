#pragma once

#include "itool.h"

class ShellExecTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
    bool requires_approval() const override { return true; }
};

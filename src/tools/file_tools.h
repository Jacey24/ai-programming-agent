#pragma once

#include "itool.h"

class FileReadTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
};

class FileWriteTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
    bool requires_approval() const override { return true; }
};

class FileEditTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    ToolResult execute(const json& params) override;
    bool requires_approval() const override { return true; }
};

#pragma once

#include <string>
#include <unordered_map>
#include "itool.h"

class SkillManager {
public:
    void load_from_dir(const std::string& dir);
    void register_skill(const std::string& name, const json& schema);
    ToolResult invoke(const std::string& name, const json& params);

private:
    std::unordered_map<std::string, json> skills_;
};

#include "skill_manager.h"

void SkillManager::load_from_dir(const std::string& dir) {
    // TODO: Scan directory for skill definitions, register each
}

void SkillManager::register_skill(const std::string& name, const json& schema) {
    skills_[name] = schema;
}

ToolResult SkillManager::invoke(const std::string& name, const json& params) {
    // TODO: Execute skill logic
    return {true, "Skill result placeholder"};
}

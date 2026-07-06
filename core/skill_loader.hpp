// skill_loader.hpp ¡ª Load skill JSON files, register commands with Shell
#pragma once
#include "shell.hpp"
#include <map>
#include <string>
#include <vector>

namespace astral {

struct SkillConfig {
  std::string name;
  std::string display_name;
  std::string description;
  std::string exe;
  std::string prompt;
  double temperature;
  bool entry;
  std::vector<std::string> transfer_to;
  std::vector<std::string> cmd_list;
  std::vector<std::string> dangerous_cmds; // ???????????

  // Context isolation
  struct {
    bool isolated = true; // true=?????, false=?????
  } ctx;

  // Execution control
  struct {
    int max_loop_rounds = 8; // ??????????
    int max_repeat_cmds = 3; // ?????????????????
    int fail_strategy = 0;   // 0=????, >0=??????
    int critical = 0;        // 0=????task, 1=FAIL??????chat
  } exec;
};

class SkillLoader {
public:
  // Load all skills from a directory
  // Returns number of loaded skills
  int load_all(const std::string &directory, Shell &shell);

  // Get loaded config by name
  const SkillConfig *get(const std::string &name) const;

  // Get entry skill name
  std::string entry_name() const { return entry_; }

  // List all skill names
  std::vector<std::string> list_skills() const;

  // Build auto-registration string for dispatcher prompt
  // Includes all non-entry skills with their name, display_name and description
  std::string build_skill_registry() const;

private:
  SkillConfig parse_file(const std::string &path, Shell &shell);
  std::map<std::string, SkillConfig> skills_;
  std::string entry_;
};

} // namespace astral
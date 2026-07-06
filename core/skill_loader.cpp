// skill_loader.cpp — Load skill JSON files, register commands with Shell
#include "skill_loader.hpp"
#include "util.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace astral {

static std::string load_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string extract_str(const std::string &j, const std::string &k,
                               const std::string &d) {
  std::string needle = "\"" + k + "\"";
  // Search from end to find the LAST occurrence (avoids matching "name" in
  // "display_name")
  auto p = j.rfind(needle);
  if (p == std::string::npos)
    p = j.find(needle);
  if (p == std::string::npos)
    return d;
  // Find colon after key
  p = j.find(':', p + k.size() + 2);
  if (p == std::string::npos)
    return d;
  // Skip whitespace
  p = j.find_first_not_of(" \t\r\n", p + 1);
  if (p == std::string::npos)
    return d;
  // Check for null
  if (j.substr(p, 4) == "null")
    return "";
  // Must be a string value starting with "
  if (j[p] != '"')
    return d;
  // Find closing quote, skipping escaped ones
  size_t p2 = p + 1;
  while (p2 < j.size()) {
    if (j[p2] == '\\') {
      p2 += 2;
    } else if (j[p2] == '"') {
      break;
    } else {
      p2++;
    }
  }
  if (p2 >= j.size())
    return d;
  return j.substr(p + 1, p2 - p - 1);
}

static double extract_dbl(const std::string &j, const std::string &k,
                          double d) {
  auto p = j.find("\"" + k + "\"");
  if (p == std::string::npos)
    return d;
  p = j.find_first_of(": ", p + k.size() + 2);
  if (p == std::string::npos)
    return d;
  p = j.find_first_not_of(": ", p + 1);
  if (p == std::string::npos)
    return d;
  std::string num;
  while (p < j.size() && (isdigit(j[p]) || j[p] == '.' || j[p] == '-'))
    num += j[p++];
  return num.empty() ? d : std::stod(num);
}

static bool extract_bool(const std::string &j, const std::string &k, bool d) {
  auto p = j.find("\"" + k + "\"");
  if (p == std::string::npos)
    return d;
  p = j.find_first_of(": ", p + k.size() + 2);
  if (p == std::string::npos)
    return d;
  p = j.find_first_not_of(": ", p + 1);
  if (p == std::string::npos)
    return d;
  return j.substr(p, 4) == "true";
}

static std::vector<std::string> extract_str_arr(const std::string &j,
                                                const std::string &k) {
  std::vector<std::string> r;
  auto p = j.find("\"" + k + "\"");
  if (p == std::string::npos)
    return r;
  auto as = j.find('[', p);
  if (as == std::string::npos)
    return r;
  auto ae = j.find(']', as);
  std::string arr = j.substr(as + 1, ae - as - 1);
  size_t sp = 0;
  while (true) {
    auto q = arr.find("\"", sp);
    if (q == std::string::npos)
      break;
    auto q2 = arr.find("\"", q + 1);
    r.push_back(arr.substr(q + 1, q2 - q - 1));
    sp = q2 + 1;
  }
  return r;
}

static std::vector<std::string> extract_cmd_keys(const std::string &j) {
  std::vector<std::string> r;
  auto p = j.find("\"commands\"");
  if (p == std::string::npos)
    return r;
  auto co = j.find('{', p);
  if (co == std::string::npos)
    return r;
  // Read everything between { } using quote-skipping char-by-char
  size_t pos = co + 1;
  while (pos < j.size() && j[pos] != '}') {
    if (j[pos] == '"') {
      auto ke = j.find('"', pos + 1);
      if (ke != std::string::npos) {
        r.push_back(j.substr(pos + 1, ke - pos - 1));
        pos = ke + 1;
      } else {
        break;
      }
    } else {
      pos++;
    }
  }
  return r;
}

SkillConfig SkillLoader::parse_file(const std::string &path, Shell &shell) {
  SkillConfig sc = {};
  std::string text = load_file(path);
  if (text.empty())
    return sc;

  sc.name = extract_str(text, "name", "");
  sc.display_name = extract_str(text, "display_name", sc.name);
  sc.description = extract_str(text, "description", "");
  sc.exe = extract_str(text, "exe", "");
  std::string raw_prompt = extract_str(text, "prompt", "");
  sc.prompt = util::json_unescape(raw_prompt);
  sc.temperature = extract_dbl(text, "temperature", 0.3);
  sc.entry = extract_bool(text, "entry", false);
  sc.transfer_to = extract_str_arr(text, "transfer_to");
  sc.cmd_list = extract_cmd_keys(text);

  // Parse new exec fields with backward compatibility
  // Try new "ctx.isolated" format first, then fall back to old "ctx" block
  sc.ctx.isolated = extract_bool(text, "isolated", true);
  // Also check the old "ctx" object's isolated for backward compat
  // (extract_bool with "isolated" will match the first "isolated" in the file)

  // exec: max_loop_rounds (fallback: old behavior.max_loop_rounds, then old
  // max_expert_calls style)
  sc.exec.max_loop_rounds = (int)extract_dbl(text, "max_loop_rounds", 8);
  // If old behavior.max_loop_rounds defined, use that
  if (text.find("\"max_loop_rounds\"") == std::string::npos &&
      text.find("\"behavior\"") != std::string::npos) {
    // Check if there's a "max_loop_rounds" inside the "behavior" block
    auto bp = text.find("\"behavior\"");
    auto be = text.find("\n", bp + 10);
    if (be == std::string::npos)
      be = text.find("}", bp);
    auto sub = text.substr(bp, be - bp + 1);
    double old_val = extract_dbl(sub, "max_loop_rounds", -1);
    if (old_val >= 0)
      sc.exec.max_loop_rounds = (int)old_val;
  }

  sc.exec.max_repeat_cmds = (int)extract_dbl(text, "max_repeat_cmds", 3);
  sc.exec.fail_strategy = (int)extract_dbl(text, "fail_strategy", 0);
  sc.exec.critical = (int)extract_dbl(text, "critical", 0);

  // Parse dangerous_commands
  sc.dangerous_cmds = extract_str_arr(text, "dangerous_commands");

  // Register commands with Shell if exe is set
  if (!sc.exe.empty()) {
    for (auto &cmd : sc.cmd_list) {
      bool dangerous = false;
      for (auto &dc : sc.dangerous_cmds) {
        if (dc == cmd) {
          dangerous = true;
          break;
        }
      }
      shell.register_cmd(cmd, sc.exe, dangerous, sc.description);
    }
  }

  if (sc.entry)
    entry_ = sc.name;

  return sc;
}

int SkillLoader::load_all(const std::string &directory, Shell &shell) {
  skills_.clear();
  entry_.clear();
  int count = 0;

  for (auto &entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() == ".json") {
      SkillConfig sc = parse_file(entry.path().string(), shell);
      if (!sc.name.empty()) {
        skills_[sc.name] = sc;
        count++;
      }
    }
  }
  return count;
}

const SkillConfig *SkillLoader::get(const std::string &name) const {
  auto it = skills_.find(name);
  return it == skills_.end() ? nullptr : &it->second;
}

std::vector<std::string> SkillLoader::list_skills() const {
  std::vector<std::string> names;
  for (auto &[k, _] : skills_)
    names.push_back(k);
  return names;
}

std::string SkillLoader::build_skill_registry() const {
  std::string registry;
  for (auto &[name, sc] : skills_) {
    if (sc.entry)
      continue; // skip dispatcher itself
    registry += "- **" + sc.name;
    if (!sc.display_name.empty() && sc.display_name != sc.name)
      registry += "（" + sc.display_name + "）";
    registry += "**";
    if (!sc.description.empty())
      registry += "：" + sc.description;
    if (!sc.cmd_list.empty()) {
      registry += "。可执行命令：";
      bool first = true;
      for (auto &cmd : sc.cmd_list) {
        if (!first)
          registry += "、";
        first = false;
        registry += cmd;
      }
    }
    registry += "\n";
  }
  return registry;
}

} // namespace astral

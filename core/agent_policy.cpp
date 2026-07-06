// agent_policy.cpp — Command mask system + workfolder/home management
// Split from original agent.cpp to improve module separation
// Cooperates with: agent_exec.cpp (called by get_mask in exec_cmd),
//                  agent_interact.cpp (called by plan_interact/process)
#include "agent.hpp"
#include "agent_types.hpp"
#include "shell.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma comment(lib, "advapi32.lib")
#include <windows.h>
#endif

namespace astral {

// ==================== Constructor ===================================
// [配合关系] 构造函数，初始化所有引用。由 main.cpp 调用。
Agent::Agent(Shell &shell, SkillLoader &loader, const AgentConfig &cfg)
    : shell_(shell), loader_(loader), cfg_(cfg) {}

// ===================================================================
// Workfolder / Home persistence
// ===================================================================
// [配合关系] 这些函数被 Agent::process(), plan_interact(), execute_resume()
// 调用 以获取当前工作目录上下文和history信息。 set_workfolder() 会触发
// load_masks() 加载该文件夹的策略文件。
std::string Agent::wf_history_path() const { return "workfolder_history.json"; }

void Agent::save_workfolder_history() const {
  std::ofstream out(wf_history_path());
  if (!out.is_open())
    return;
  out << "{\n";
  if (!home_folder_.empty())
    out << "  \"home\": \"" << home_folder_ << "\",\n";
  out << "  \"history\": [\n";
  for (size_t i = 0; i < wf_history_.size(); i++) {
    if (i > 0)
      out << ",\n";
    out << "    \"" << wf_history_[i] << "\"";
  }
  out << "\n  ]\n}\n";
  out.close();
}

void Agent::load_workfolder_history() {
  wf_history_.clear();
  home_folder_.clear();
  std::ifstream in(wf_history_path());
  if (!in.is_open())
    return;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  in.close();

  auto home_key = content.find("\"home\"");
  if (home_key != std::string::npos) {
    auto colon = content.find(':', home_key + 6);
    if (colon != std::string::npos) {
      auto q1 = content.find('"', colon);
      if (q1 != std::string::npos) {
        auto q2 = content.find('"', q1 + 1);
        if (q2 != std::string::npos) {
          home_folder_ = content.substr(q1 + 1, q2 - q1 - 1);
        }
      }
    }
  }

  auto as = content.find('[');
  if (as == std::string::npos)
    return;
  auto sub = content.substr(as + 1);
  size_t pos = 0;
  while (true) {
    auto q = sub.find('"', pos);
    if (q == std::string::npos)
      break;
    auto q2 = sub.find('"', q + 1);
    if (q2 == std::string::npos)
      break;
    wf_history_.push_back(sub.substr(q + 1, q2 - q - 1));
    pos = q2 + 1;
  }
}

void Agent::load_workfolder_state() {
  load_workfolder_history();
  if (!wf_history_.empty()) {
    set_workfolder(wf_history_[0]);
  }
}

void Agent::save_workfolder_state() const { save_workfolder_history(); }

void Agent::set_workfolder(const std::string &path) {
  workfolder_ = path;
  if (!workfolder_.empty()) {
    for (auto &c : workfolder_)
      if (c == '/')
        c = '\\';
    if (workfolder_.back() != '\\')
      workfolder_ += '\\';
  }
  if (!workfolder_.empty()) {
    auto it = std::find(wf_history_.begin(), wf_history_.end(), workfolder_);
    if (it != wf_history_.end())
      wf_history_.erase(it);
    wf_history_.insert(wf_history_.begin(), workfolder_);
    if ((int)wf_history_.size() > MAX_WF_HISTORY)
      wf_history_.resize(MAX_WF_HISTORY);
    save_workfolder_history();
  }
  load_masks();
}

std::vector<std::string> Agent::workfolder_history() const {
  return wf_history_;
}

// ===================================================================
// Command Mask System (3-state: NORMAL / BLOCK / APPROVE)
// ===================================================================
// [配合关系]
//   - load_masks(): 由 set_workfolder() 在切换目录时触发
//   - get_mask(): 由 agent_exec.cpp 中的 exec_cmd() 在危险命令执行前调用
//   - mask_status(): 由 main.cpp 的 /mask CLI 命令使用
//   - set_memory_mask(): 提供内存级别的默认掩码（不写文件）
std::string Agent::mask_path() const { return workfolder_ + "cmd_masks.json"; }

void Agent::load_masks() {
  mask_map_.clear();
  if (workfolder_.empty())
    return;
  std::string path = mask_path();
  if (!std::filesystem::exists(path))
    return;
  std::ifstream in(path);
  if (!in.is_open())
    return;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  in.close();
  auto ob_start = content.find('{');
  auto ob_end = content.find('}', ob_start);
  if (ob_start == std::string::npos || ob_end == std::string::npos)
    return;
  std::string obj = content.substr(ob_start + 1, ob_end - ob_start - 1);
  size_t pos = 0;
  while (true) {
    auto q = obj.find('"', pos);
    if (q == std::string::npos)
      break;
    auto q2 = obj.find('"', q + 1);
    if (q2 == std::string::npos)
      break;
    std::string cmd = obj.substr(q + 1, q2 - q - 1);
    auto col = obj.find(':', q2);
    if (col == std::string::npos || col >= ob_end)
      break;
    auto vq = obj.find('"', col);
    if (vq == std::string::npos || vq >= ob_end)
      break;
    auto vq2 = obj.find('"', vq + 1);
    if (vq2 == std::string::npos || vq2 >= ob_end)
      break;
    std::string val = obj.substr(vq + 1, vq2 - vq - 1);
    std::string upper = cmd;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (val == "block")
      mask_map_[upper] = MASK_BLOCK;
    else if (val == "approve")
      mask_map_[upper] = MASK_APPROVE;
    pos = vq2 + 1;
  }
  log_("[MASKS] Loaded " + std::to_string(mask_map_.size()) +
       " mask rules from " + path);
}

void Agent::save_masks() const {
  if (workfolder_.empty())
    return;
  std::filesystem::create_directories(workfolder_);
  std::ofstream out(mask_path());
  if (!out.is_open()) {
    log_("[MASKS] Cannot write: " + mask_path());
    return;
  }
  out << "{\n";
  size_t count = 0;
  for (auto &[cmd, action] : mask_map_) {
    if (count > 0)
      out << ",\n";
    std::string val = (action == MASK_BLOCK) ? "block" : "approve";
    out << "  \"" << cmd << "\": \"" << val << "\"";
    count++;
  }
  out << "\n}\n";
  out.close();
  log_("[MASKS] Saved " + std::to_string(count) + " rules");
}

void Agent::set_mask(const std::string &cmd, MaskAction action) {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  if (action == MASK_NORMAL) {
    mask_map_.erase(upper);
  } else {
    mask_map_[upper] = action;
  }
  save_masks();
  std::string action_str = (action == MASK_BLOCK)     ? "BLOCK"
                           : (action == MASK_APPROVE) ? "APPROVE"
                                                      : "NORMAL";
  log_("[MASKS] Set " + upper + " → " + action_str);
}

Agent::MaskAction Agent::get_mask(const std::string &cmd) const {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  auto it = mask_map_.find(upper);
  if (it != mask_map_.end())
    return it->second;
  auto mem_it = memory_mask_map_.find(upper);
  if (mem_it != memory_mask_map_.end())
    return mem_it->second;
  return MASK_NORMAL;
}

void Agent::set_memory_mask(const std::string &cmd, MaskAction action) {
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  if (action == MASK_NORMAL) {
    memory_mask_map_.erase(upper);
  } else {
    memory_mask_map_[upper] = action;
  }
  std::string action_str = (action == MASK_BLOCK)     ? "BLOCK"
                           : (action == MASK_APPROVE) ? "APPROVE"
                                                      : "NORMAL";
  log_("[MEMORY_MASK] Set " + upper + " → " + action_str);
}

void Agent::set_home_folder(const std::string &path) {
  home_folder_ = path;
  if (!home_folder_.empty()) {
    for (auto &c : home_folder_)
      if (c == '/')
        c = '\\';
    if (home_folder_.back() != '\\')
      home_folder_ += '\\';
  }
  save_workfolder_history();
  log_("[HOME] Home folder set to: " + home_folder_);
}

std::string Agent::mask_status(const std::string &cmd) const {
  if (!cmd.empty()) {
    std::string upper = cmd;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    MaskAction combined = get_mask(upper);
    std::string s = "指令 " + upper + " 当前状态：\n";
    auto fit = mask_map_.find(upper);
    if (fit != mask_map_.end()) {
      s += "  📁 文件夹策略: " +
           std::string(fit->second == MASK_BLOCK ? "🔴 阻止" : "✅ 自动批准") +
           "\n";
      s += "     配置文件: " + mask_path() + "\n";
    } else {
      s += "  📁 文件夹策略: 未设置\n";
    }
    auto mit = memory_mask_map_.find(upper);
    if (mit != memory_mask_map_.end()) {
      s += "  🧠 内存默认: " +
           std::string(mit->second == MASK_BLOCK ? "🔴 阻止" : "✅ 自动批准") +
           "\n";
    } else {
      s += "  🧠 内存默认: 未设置\n";
    }
    s += "  ➡️ 生效策略: " +
         std::string(combined == MASK_BLOCK     ? "🔴 阻止"
                     : combined == MASK_APPROVE ? "✅ 自动批准"
                                                : "⚪ 默认（询问确认）") +
         "\n";
    return s;
  }

  std::map<std::string, std::pair<bool, bool>> cmd_states;
  for (auto &[c, a] : mask_map_)
    cmd_states[c].first = true;
  for (auto &[c, a] : memory_mask_map_)
    cmd_states[c].second = true;

  auto all_cmds = shell_.list_commands_with_dangerous();
  for (auto &[c, d] : all_cmds) {
    if (d)
      cmd_states.try_emplace(c, false, false);
  }

  if (cmd_states.empty())
    return "系统中没有已注册的危险指令。\n";

  std::string s;
  if (!workfolder_.empty())
    s = "当前工作目录: " + workfolder_ + "\n";
  else
    s = "当前未设置工作目录。\n";
  if (!home_folder_.empty())
    s += "Home 文件夹: " + home_folder_ + "\n";
  s += "\n指令状态：\n";
  for (auto &[cmd_name, states] : cmd_states) {
    MaskAction combined = get_mask(cmd_name);
    s += "  " + cmd_name + " → ";
    s += (combined == MASK_BLOCK)     ? "🔴 阻止"
         : (combined == MASK_APPROVE) ? "✅ 自动批准"
                                      : "⚪ 询问确认";
    if (states.first)
      s += " [📁 文件夹策略]";
    else if (states.second)
      s += " [🧠 内存默认]";
    s += "\n";
  }
  return s;
}

} // namespace astral
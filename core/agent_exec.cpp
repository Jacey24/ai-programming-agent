// agent_exec.cpp — Core execution engine: command execution, plan execution,
//                   analyser recovery, context building.
// Split from original agent.cpp.
// [配合关系]
//   - exec_cmd(): 调用 agent_policy.cpp 的 get_mask() 检查危险命令掩码
//   - execute_plan(): 被 agent_interact.cpp 的
//   process()/execute_outline()/execute_resume() 调用
//   - invoke_analyser(): 在 execute_plan() 内部使用
//   - build_expert_context(): 被 execute_plan() 内部使用
//   - parse_all_cmds(): 被 execute_plan() 内部使用
#include "../runtime/api_client.hpp"
#include "../runtime/xml_protocol.hpp"
#include "agent.hpp"
#include "agent_types.hpp"
#include "util.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma comment(lib, "advapi32.lib")
#include <lmcons.h>
#include <windows.h>
#endif

namespace astral {

// ==================== Static Helpers ================================

// Convert GBK string to UTF-8 (for subprocess output on Chinese Windows)
static std::string gbk_to_utf8(const std::string &gbk) {
  if (gbk.empty())
    return gbk;
#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), -1, nullptr, 0);
  if (wlen <= 0)
    return gbk;
  std::wstring wstr(wlen, L'\0');
  MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), -1, &wstr[0], wlen);
  int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0,
                                 nullptr, nullptr);
  if (ulen <= 0)
    return gbk;
  std::string utf8(ulen, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], ulen, nullptr,
                      nullptr);
  if (!utf8.empty())
    utf8.pop_back();
  return utf8;
#else
  return gbk;
#endif
}

// Simple JSON string writer helper
static std::string json_key_val(const std::string &key, const std::string &val,
                                bool is_last = false) {
  return "\"" + key + "\":\"" + val + "\"" + (is_last ? "" : ",");
}

// Check if a string is already valid UTF-8
static bool is_valid_utf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    if (c <= 0x7F) {
      i++;
    } else if (c >= 0xC2 && c <= 0xDF) {
      if (i + 1 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80)
        return false;
      i += 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
      if (i + 2 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
    } else if (c >= 0xF0 && c <= 0xF4) {
      if (i + 3 >= s.size() || ((unsigned char)s[i + 1] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 2] & 0xC0) != 0x80 ||
          ((unsigned char)s[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

// Parse ALL <cmd> tags from a single AI output
static std::vector<std::string> parse_all_cmds(const std::string &output) {
  std::vector<std::string> cmds;
  size_t pos = 0;
  while (true) {
    auto start = output.find("<cmd>", pos);
    if (start == std::string::npos)
      break;
    start += 5;
    auto end = output.find("</cmd>", start);
    if (end == std::string::npos)
      break;
    cmds.push_back(output.substr(start, end - start));
    pos = end + 6;
  }
  return cmds;
}

// Resolve relative paths: if path is not absolute and workfolder is set,
// prepend workfolder to the path.
static std::string resolve_cmd_paths(const std::string &cmd_line,
                                     const std::string &workfolder) {
  if (workfolder.empty())
    return cmd_line;
  std::istringstream iss(cmd_line);
  std::string cmd;
  iss >> cmd;
  std::string upper = cmd;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  static const char *file_cmds[] = {"READ", "APPEND", "WRITE",
                                    "LINE", "DELETE", "RENAME"};
  bool is_file_cmd = false;
  for (auto *fc : file_cmds) {
    if (upper == fc) {
      is_file_cmd = true;
      break;
    }
  }
  if (!is_file_cmd)
    return cmd_line;
  std::vector<std::string> tokens;
  std::string token;
  while (iss >> token)
    tokens.push_back(token);
  std::string result = cmd;
  int ti = 0;
  if (ti < (int)tokens.size() && tokens[ti].size() == 2 &&
      tokens[ti][0] == '-') {
    result += " " + tokens[ti];
    ti++;
  }
  std::string path_token;
  if (ti < (int)tokens.size()) {
    path_token = tokens[ti];
    auto ppos = path_token.find('|');
    if (ppos != std::string::npos)
      path_token = path_token.substr(0, ppos);
  }
  if (!path_token.empty() &&
      !(path_token.size() >= 2 && path_token[1] == ':') &&
      path_token[0] != '\\' && path_token[0] != '/') {
    result += " " + workfolder + path_token;
    ti++;
  } else {
    if (ti < (int)tokens.size()) {
      result += " " + tokens[ti];
      ti++;
    }
  }
  for (; ti < (int)tokens.size(); ti++)
    result += " " + tokens[ti];
  return result;
}

// Build a formatted log string from round_history for a single task
static std::string build_round_log(
    const std::string &task,
    const std::vector<std::pair<std::string, std::vector<std::string>>>
        &rounds) {
  std::string log = "[任务: " + task + "]\n";
  for (size_t r = 0; r < rounds.size(); r++) {
    log += "--- 第 " + std::to_string(r + 1) + " 轮 ---\n";
    log += "AI输出: " + rounds[r].first + "\n";
    for (auto &cr : rounds[r].second)
      log += "  结果: " + cr + "\n";
  }
  return log;
}

// Build system info string
static std::string build_system_info() {
  std::string info = "当前系统环境:\n";
#ifdef _WIN32
  char username[UNLEN + 1];
  DWORD ulen = UNLEN + 1;
  if (GetUserNameA(username, &ulen)) {
    char compname[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD clen = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(compname, &clen))
      info += "- 用户: " + std::string(username) + "@" + std::string(compname) +
              "\n";
    else
      info += "- 用户: " + std::string(username) + "\n";
  }
#endif
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  localtime_s(&tm, &tt);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  info += "- 当前时间: " + std::string(buf) + "\n";
  return info;
}

// Generate ISO timestamp string
static std::string now_iso() {
  std::time_t t = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
  return buf;
}

// ==================== Agent methods =================================

void Agent::set_logger(std::function<void(const std::string &)> log) {
  log_ = log;
}

void Agent::set_confirmer(
    std::function<bool(const std::string &skill, std::string &cmd_line)>
        confirmer) {
  confirmer_ = confirmer;
}

ApiClient &Agent::api() {
  if (!api_)
    api_ = new ApiClient(cfg_);
  return *api_;
}

CallRecord Agent::make_call(const std::string &skill_name,
                            const std::string &system_prompt,
                            const std::string &user_input, double temperature) {
  CallRecord rec;
  rec.skill = skill_name;
  auto cr = api().chat(system_prompt, user_input, temperature);
  rec.api_content = cr.content;
  rec.raw_response = cr.raw_json;
  ApiClient::extract_tokens(cr.raw_json, rec.tokens.prompt_tokens,
                            rec.tokens.completion_tokens,
                            rec.tokens.total_tokens);
  return rec;
}

// Build prompt extras for expert context
std::string Agent::build_expert_context(
    const std::string &task, const std::string &main_context,
    const std::vector<TaskResult> &results, bool isolated,
    const std::vector<std::string> &cmd_results) {
  std::string extra;
  extra += "[系统信息]\n" + build_system_info() + "\n";
  if (!workfolder_.empty()) {
    extra += "[工作目录: " + workfolder_ + "]\n\n";
  }
  extra += "[TASK: " + task + "]\n";

  if (!results.empty()) {
    extra += "[已完成步骤]\n";
    for (auto &tr : results)
      extra += "- " + tr.skill + ": " + tr.summary +
               (tr.succeeded ? "" : " (失败)") + "\n";
  }

  if (isolated) {
    if (!cmd_results.empty()) {
      extra += "[当前任务执行记录]\n";
      for (auto &cr : cmd_results)
        extra += "  - " + cr + "\n";
    }
    if (!main_context.empty()) {
      extra += "[CONTEXT: " + main_context + "]\n";
    }
  } else {
    extra += "[CONTEXT: " + main_context + "]\n";
  }
  return extra;
}

// Task Context Summary (for /resume CLI and analyser)
const std::string Agent::build_task_context_summary() const {
  if (task_context_.empty())
    return "当前没有任务执行记录。\n";
  std::string s = "=== 任务执行历史 ===\n";
  for (size_t i = 0; i < task_context_.size(); i++) {
    auto &e = task_context_[i];
    s += "[" + std::to_string(i) + "] " + e.skill + ": " + e.task + "\n";
    s += "   结果: " +
         (e.succeeded ? std::string("OK ") : std::string("FAIL ")) + e.summary +
         "\n";
  }
  return s;
}

// Exec cmd — handles special internal commands too
// [配合关系] 调用 agent_policy.cpp 的 get_mask() 检查危险命令状态
std::string Agent::exec_cmd(const std::string &cmd_line) {
  std::string cmd_name;
  std::istringstream iss_cmd(cmd_line);
  iss_cmd >> cmd_name;
  std::string upper = cmd_name;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  // ---- Special internal commands ----
  if (upper == "SETWF") {
    if (confirmer_) {
      std::string modified = cmd_line;
      if (!confirmer_("SETWF", modified)) {
        return "{\"ok\":false,\"msg\":\"切换工作目录已取消\",\"data\":{}}\n";
      }
    }
    std::string path;
    std::istringstream iss(cmd_line);
    std::string dummy;
    iss >> dummy;
    std::getline(iss, path);
    path.erase(0, path.find_first_not_of(" \t"));
    path.erase(path.find_last_not_of(" \t") + 1);
    if (path.empty()) {
      return "{\"ok\":false,\"msg\":\"请提供工作目录路径\",\"data\":{}}\n";
    }
    set_workfolder(path);
    log_("[SETWF] Workfolder → " + workfolder_);
    return "{\"ok\":true,\"msg\":\"工作目录已切换: " + workfolder_ +
           "\",\"data\":{\"workfolder\":\"" + workfolder_ + "\"}}\n";
  }

  if (upper == "LISTCMDS") {
    auto cmds = shell_.list_commands_with_dangerous();
    std::string data =
        "{\"count\":" + std::to_string(cmds.size()) + ",\"commands\":[";
    bool first = true;
    for (auto &[c, d] : cmds) {
      if (!first)
        data += ",";
      first = false;
      data += "{\"cmd\":\"" + c + "\",\"dangerous\":" + (d ? "true" : "false") +
              "}";
    }
    data += "]}";
    std::string msg =
        "当前系统注册了 " + std::to_string(cmds.size()) + " 条可用指令";
    log_("[LISTCMDS] " + msg);
    return "{\"ok\":true,\"msg\":\"" + msg + "\",\"data\":" + data + "}\n";
  }

  // ---- Normal command execution path ----
  std::string resolved = resolve_cmd_paths(cmd_line, workfolder_);
  if (resolved != cmd_line)
    log_("[PATH] Resolved: " + cmd_line + " → " + resolved);

  std::string resolved_cmd;
  std::istringstream iss2(resolved);
  iss2 >> resolved_cmd;
  std::string upper2 = resolved_cmd;
  std::transform(upper2.begin(), upper2.end(), upper2.begin(), ::toupper);

  if (shell_.is_dangerous(upper2)) {
    MaskAction ma = get_mask(upper2);
    if (ma == MASK_BLOCK) {
      log_("[MASK] BLOCKED: " + upper2);
      return "{\"ok\":false,\"msg\":\"该指令已被工作目录的掩码阻止: " + upper2 +
             "\",\"data\":{\"mask\":\"block\"}}\n";
    }
    if (ma == MASK_APPROVE) {
      log_("[MASK] AUTO-APPROVED: " + upper2);
      auto sr = shell_.run(resolved);
      if (sr.ok) {
        if (is_valid_utf8(sr.stdout_text))
          return sr.stdout_text;
        return gbk_to_utf8(sr.stdout_text);
      }
      return "Error: " + sr.stderr_text;
    }
    if (confirmer_) {
      std::string modified_cmd = resolved;
      if (!confirmer_(upper2, modified_cmd)) {
        return "{\"ok\":false,\"msg\":\"危险操作已取消: " + upper2 +
               "\",\"data\":{}}\n";
      }
      if (modified_cmd != resolved) {
        log_("[CONFIRM] Modified by user");
        auto sr = shell_.run(modified_cmd);
        if (sr.ok) {
          if (is_valid_utf8(sr.stdout_text))
            return sr.stdout_text;
          return gbk_to_utf8(sr.stdout_text);
        }
        return "Error: " + sr.stderr_text;
      }
    }
  }

  auto sr = shell_.run(resolved);
  if (sr.ok) {
    if (is_valid_utf8(sr.stdout_text))
      return sr.stdout_text;
    return gbk_to_utf8(sr.stdout_text);
  }
  return "Error: " + sr.stderr_text;
}

// ===================================================================
// Invoke the analyser expert on task failure
// ===================================================================
bool Agent::invoke_analyser(const std::string &fail_context,
                            const std::string &full_task_log,
                            const std::string &task_summary,
                            PlanResult &out_plan, CallRecord &out_call_record) {
  auto *an_sc = loader_.get("analyser");
  if (!an_sc) {
    log_("[ANALYSER] Analyser skill not registered");
    return false;
  }

  std::string ctx_summary = "=== 任务执行历史 ===\n";
  for (size_t i = 0; i < task_context_.size(); i++) {
    auto &e = task_context_[i];
    ctx_summary += "[" + std::to_string(i) + "] " + e.skill + ": " + e.task +
                   "\n  结果: " +
                   (e.succeeded ? std::string("OK ") : std::string("FAIL ")) +
                   e.summary + "\n";
  }

  std::string analyser_input =
      "以下是从历史到现在的完整任务记录：\n\n" + ctx_summary + "\n" +
      "===== 当前失败任务 ====\n" + full_task_log + "\n" +
      "失败原因: " + task_summary + "\n\n" +
      "请分析失败原因，提出新的修复方案，输出 <plan> 定义新任务序列。\n"
      "输出格式：\n"
      "<plan>\n"
      "  <item skill=\"txt\" task=\"具体任务1\" />\n"
      "  <item skill=\"sys\" task=\"具体任务2\" />\n"
      "</plan>\n"
      "DONE 分析完成\n"
      "或者如果无法恢复直接输出 FAIL。";

  log_("[ANALYSER] Invoking analyser...");
  out_call_record =
      make_call("analyser", an_sc->prompt, analyser_input, an_sc->temperature);

  if (out_call_record.api_content.empty()) {
    log_("[ANALYSER] Empty response");
    return false;
  }

  if (XmlProtocol::parse_plan(out_call_record.api_content, out_plan) &&
      !out_plan.tasks.empty()) {
    log_("[ANALYSER] Produced new plan with " +
         std::to_string(out_plan.tasks.size()) + " tasks");
    return true;
  }

  log_("[ANALYSER] No plan produced");
  return false;
}

// ===================================================================
// Core execute_plan() — shared by process() and execute_outline()
// ===================================================================
// [配合关系] 被 agent_interact.cpp 的
// process()/execute_outline()/execute_resume() 调用 内部调用 invoke_analyser()
// (自身), build_expert_context() (自身)
ExecutePlanResult Agent::execute_plan(
    const PlanResult &plan, int start_index, const std::string &initial_context,
    const std::vector<TaskResult> &initial_results, int resume_depth) {
  ExecutePlanResult result;
  result.final_context = initial_context;
  result.task_results = initial_results;
  bool previous_succeeded = true;
  bool abort_all = false;

  for (auto &tr : initial_results) {
    result.flat_cmd_results.push_back("[" + tr.skill + "] " + tr.summary +
                                      (tr.succeeded ? "" : " (失败)"));
  }

  for (size_t i = start_index; i < plan.tasks.size(); i++) {
    if (abort_all) {
      log_("[ABORT] Critical abort — skipping remaining tasks");
      break;
    }

    auto &ti = plan.tasks[i];
    if (ti.fallback && previous_succeeded)
      continue;
    if (ti.skill == "chat") {
      log_("[SKIP] Chat task skipped");
      continue;
    }

    log_("[TASK] " + ti.skill + ": " + ti.task);

    auto *sc = loader_.get(ti.skill);
    if (!sc) {
      log_("[ERROR] Unknown skill: " + ti.skill);
      result.task_results.push_back({ti.skill, "未知专家", false});
      previous_succeeded = false;
      continue;
    }

    bool task_complete = false;
    bool task_succeeded = true;
    std::string task_summary;
    std::vector<std::string> cmd_results;
    int round = 0;

    struct RoundRecord {
      std::string ai_output;
      std::vector<std::string> cmd_results;
    };
    std::vector<RoundRecord> round_history;

    int max_retries =
        std::max(1, sc->exec.fail_strategy == 0 ? 1 : sc->exec.fail_strategy);

    for (int retry = 0; retry < max_retries && !task_complete; retry++) {
      if (retry > 0) {
        log_("[RETRY " + std::to_string(retry) + "/" +
             std::to_string(sc->exec.fail_strategy) + "] " + ti.skill);
        if (sc->ctx.isolated) {
          round_history.clear();
          cmd_results.clear();
        }
      }

      while (!task_complete && round < sc->exec.max_loop_rounds) {
        round++;

        std::string expert_input;
        std::string context = build_expert_context(
            ti.task, result.final_context, result.task_results,
            sc->ctx.isolated, cmd_results);

        if (!round_history.empty()) {
          context += "\n【之前各轮的完整记录】\n";
          for (size_t r = 0; r < round_history.size(); r++) {
            auto &rr = round_history[r];
            context += "===== 第 " + std::to_string(r + 1) + " 轮 =====\n";
            context += "你输出了：\n" + rr.ai_output + "\n";
            context += "系统返回了以下结果：\n";
            if (rr.cmd_results.empty()) {
              context += "  (本轮没有执行命令)\n";
            } else {
              for (auto &cr : rr.cmd_results)
                context += "  - " + cr + "\n";
            }
          }
          context += "\n";
        }

        if (retry > 0 && round == 1) {
          expert_input = "[系统提示: 这是第" + std::to_string(retry) +
                         "次重试] 请重新规划并执行任务。";
        } else if (round_history.empty()) {
          expert_input = "请规划并执行被分配的任务。你可以输出多个 <cmd> "
                         "标签来批量执行命令。";
        } else {
          expert_input = "请根据以上所有记录继续执行任务。\n"
                         "如果需要更多操作，请输出 <cmd> 标签。\n"
                         "如果任务已完成，请输出 DONE + 总结。\n"
                         "如果无法完成，请输出 FAIL + 原因。";
        }

        auto expert_call =
            make_call(ti.skill, sc->prompt, context + "\n" + expert_input,
                      sc->temperature);
        std::string output = expert_call.api_content;

        if (output.empty()) {
          log_("[ERROR] Expert " + ti.skill + " returned empty");
          task_summary = "任务执行出错";
          task_succeeded = false;
          task_complete = true;
          break;
        }

        std::vector<std::string> ai_cmds = parse_all_cmds(output);
        std::vector<std::string> round_cmd_results;

        if (!ai_cmds.empty() && !round_history.empty()) {
          auto &prev_round = round_history.back();
          auto prev_cmds = parse_all_cmds(prev_round.ai_output);
          if (ai_cmds == prev_cmds && !ai_cmds.empty()) {
            log_("[REPEAT] Same command batch, triggering failure");
            for (auto &cl : ai_cmds) {
              log_("[CMD] " + ti.skill + ": " + cl);
              std::string cr2 = exec_cmd(cl);
              log_("[RESULT] " + cr2);
              round_cmd_results.push_back(cl + " → " + cr2);
              cmd_results.push_back(cl + " → " + cr2);
            }
            task_summary = "重复指令死锁";
            task_succeeded = false;
            task_complete = true;
            break;
          }
        }

        for (auto &cl : ai_cmds) {
          log_("[CMD] " + ti.skill + ": " + cl);
          std::string cr2 = exec_cmd(cl);
          log_("[RESULT] " + cr2);
          round_cmd_results.push_back(cl + " → " + cr2);
          cmd_results.push_back(cl + " → " + cr2);
        }

        RoundRecord rr;
        rr.ai_output = output;
        rr.cmd_results = round_cmd_results;
        round_history.push_back(rr);

        std::string marker_msg;
        int marker = XmlProtocol::has_final_marker(output, marker_msg);
        if (marker == 1) {
          task_complete = true;
          task_succeeded = true;
          task_summary = marker_msg;
          log_("[DONE] " + ti.skill + ": " + task_summary);
          break;
        } else if (marker == 2) {
          task_complete = true;
          task_succeeded = false;
          task_summary = marker_msg.empty() ? "专家报告失败" : marker_msg;
          log_("[FAIL] " + ti.skill + ": " + task_summary);
          break;
        }

        if (!ai_cmds.empty())
          continue;
      }

      if (!task_complete) {
        log_("[MAX_ROUNDS] " + ti.skill + " exceeded max_loop_rounds");
        task_summary = "达到最大递归轮次";
        task_succeeded = false;
        task_complete = true;
      }

      if (!task_succeeded && retry < max_retries - 1) {
        task_complete = false;
        task_succeeded = true;
      }
    }

    // Build full task log for PlanEntry
    std::vector<std::pair<std::string, std::vector<std::string>>> round_pairs;
    for (auto &rr : round_history) {
      round_pairs.push_back({rr.ai_output, rr.cmd_results});
    }
    std::string full_task_log = build_round_log(ti.task, round_pairs);

    // Failure handling with analyser
    if (!task_succeeded) {
      if (sc->exec.fail_strategy == 0 && sc->exe.empty()) {
        log_("[FAIL_SUMMARY] Asking " + ti.skill + " to summarize");
        std::string fail_context = build_expert_context(
            ti.task, result.final_context, result.task_results,
            sc->ctx.isolated, cmd_results);
        if (!round_history.empty()) {
          fail_context += "\n【执行记录】\n";
          for (size_t r = 0; r < round_history.size(); r++) {
            auto &rr = round_history[r];
            fail_context += "===== 第 " + std::to_string(r + 1) + " 轮 =====\n";
            fail_context += "你输出了：\n" + rr.ai_output + "\n";
            fail_context += "系统返回了以下结果：\n";
            for (auto &cr : rr.cmd_results)
              fail_context += "  - " + cr + "\n";
          }
        }
        fail_context +=
            "\n[系统提示] 任务执行失败：" + task_summary +
            "\n请根据以上操作记录，输出 FAIL 并附上失败原因的详细摘要。";
        auto fail_call =
            make_call(ti.skill, sc->prompt, fail_context, sc->temperature);
        std::string fail_msg;
        int fm = XmlProtocol::has_final_marker(fail_call.api_content, fail_msg);
        if (fm == 2)
          task_summary = fail_msg;
        else
          task_summary = fail_call.api_content;
      }

      if (sc->exec.critical) {
        log_("[CRITICAL] " + ti.skill + " failed — aborting all");
        abort_all = true;
      }

      // Try analyser recovery
      if (!abort_all && resume_depth < MAX_RESUME_DEPTH) {
        PlanResult new_plan;
        CallRecord call_rec;
        if (invoke_analyser(result.final_context, full_task_log, task_summary,
                            new_plan, call_rec)) {
          log_("[ANALYSER] Recovery plan received (depth=" +
               std::to_string(resume_depth + 1) + ")");
          result.was_resumed = true;

          PlanEntry pe;
          pe.plan_index = plan_counter_++;
          pe.task_index = (int)i;
          pe.skill = ti.skill;
          pe.task = ti.task;
          pe.full_log = full_task_log;
          pe.summary = task_summary;
          pe.succeeded = false;
          task_context_.push_back(pe);

          for (auto &cr : cmd_results)
            result.flat_cmd_results.push_back(cr);
          if (!task_summary.empty())
            result.flat_cmd_results.push_back("[" + ti.skill +
                                              " 失败]: " + task_summary);

          if (!cmd_results.empty()) {
            result.final_context += "\n[" + ti.skill + " 执行结果]\n";
            for (auto &cr : cmd_results)
              result.final_context += "  - " + cr + "\n";
          }
          result.final_context += "\n[" + ti.skill + " 失败]: " + task_summary;
          result.task_results.push_back(
              {ti.skill, task_summary, task_succeeded});

          ExecutePlanResult resumed =
              execute_plan(new_plan, 0, result.final_context,
                           result.task_results, resume_depth + 1);

          result.final_context = resumed.final_context;
          result.task_results = resumed.task_results;
          result.flat_cmd_results.insert(result.flat_cmd_results.end(),
                                         resumed.flat_cmd_results.begin(),
                                         resumed.flat_cmd_results.end());
          result.all_succeeded = resumed.all_succeeded;
          previous_succeeded = resumed.all_succeeded;
          continue;
        } else {
          log_("[ANALYSER] No recovery plan available");
        }
      }

      previous_succeeded = false;
    } else {
      previous_succeeded = true;
    }

    // Append PlanEntry to task_context_
    PlanEntry pe;
    pe.plan_index = plan_counter_++;
    pe.task_index = (int)i;
    pe.skill = ti.skill;
    pe.task = ti.task;
    pe.full_log = full_task_log;
    pe.summary = task_summary;
    pe.succeeded = task_succeeded;
    task_context_.push_back(pe);

    // Update accumulated context
    if (!cmd_results.empty()) {
      result.final_context += "\n[" + ti.skill + " 执行结果]\n";
      for (auto &cr : cmd_results) {
        result.final_context += "  - " + cr + "\n";
        result.flat_cmd_results.push_back(cr);
      }
    }
    if (!task_succeeded) {
      result.final_context += "\n[" + ti.skill + " 失败]: " + task_summary;
    } else {
      result.final_context += "\n[" + ti.skill + " 完成]: " + task_summary;
    }
    result.task_results.push_back({ti.skill, task_summary, task_succeeded});
  }

  result.all_succeeded = !abort_all;
  for (auto &tr : result.task_results) {
    if (!tr.succeeded && !result.was_resumed)
      result.all_succeeded = false;
  }

  return result;
}

} // namespace astral
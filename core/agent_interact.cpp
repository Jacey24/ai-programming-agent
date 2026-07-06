// agent_interact.cpp — Agent interaction layer: process(), plan_interact(),
//                       execute_outline(), execute_resume().
// Split from original agent.cpp.
// [配合关系]
//   - process(): 调用 agent_exec.cpp 的 execute_plan()
//   - plan_interact(): 独立模式，处理规划对话
//   - execute_outline(): 调用 agent_exec.cpp 的 execute_plan()
//   - execute_resume(): 调用 agent_exec.cpp 的 execute_plan()
#include "../runtime/xml_protocol.hpp"
#include "agent.hpp"
#include "agent_types.hpp"
#include <algorithm>
#include <sstream>


namespace astral {

// ===================================================================
// ACT mode: process()
// ===================================================================
AgentResult Agent::process(const std::string &input,
                           const std::string &conversation_history) {
  AgentResult result;

  log_("[PLAN] " + loader_.entry_name());
  std::string dispatcher_prompt = loader_.get(loader_.entry_name())->prompt;
  std::string registry = loader_.build_skill_registry();
  if (!registry.empty()) {
    dispatcher_prompt += "\n\n【当前已注册的所有可用专家】\n" + registry;
  }
  if (!conversation_history.empty()) {
    dispatcher_prompt += "\n\n【对话历史】\n" + conversation_history + "\n";
  }

  auto plan_call = make_call(loader_.entry_name(), dispatcher_prompt, input,
                             loader_.get(loader_.entry_name())->temperature);
  result.records.push_back(plan_call);
  std::string plan_output = plan_call.api_content;
  if (plan_output.empty()) {
    log_("[ERROR] Dispatcher returned empty");
    result.reply = "Sorry, failed to process.";
    return result;
  }

  PlanResult plan;
  if (!XmlProtocol::parse_plan(plan_output, plan)) {
    std::string chat_content;
    if (XmlProtocol::has_chat(plan_output, chat_content)) {
      log_("[CHAT] Direct chat from dispatcher");
      auto chat_call = make_call("chat", loader_.get("chat")->prompt,
                                 "[用户原话: " + input + "]\n" + chat_content,
                                 loader_.get("chat")->temperature);
      result.records.push_back(chat_call);
      result.reply = chat_call.api_content;
      return result;
    }
    log_("[PLAN] No plan found");
    result.reply = plan_output;
    return result;
  }

  std::string main_context = input;

  ExecutePlanResult epr = execute_plan(plan, 0, main_context, {}, 0);

  // Build chat summary
  auto *chat_sc = loader_.get("chat");
  if (!chat_sc) {
    result.reply = "任务执行结果:\n" + epr.final_context;
    return result;
  }

  std::string chat_extra;
  chat_extra += "[已完成的任务]\n";
  for (auto &tr : epr.task_results)
    chat_extra += "- " + tr.skill + ": " + tr.summary +
                  (tr.succeeded ? "" : " (失败)") + "\n";
  chat_extra += "[CONTEXT: " + epr.final_context + "]\n";
  chat_extra += "[用户原话: " + input + "]";
  if (epr.was_resumed) {
    chat_extra += "\n[系统提示] 任务曾在失败后自动恢复重试。";
  }

  log_("[CHAT] Summarizing");
  auto chat_call =
      make_call("chat", chat_sc->prompt, chat_extra, chat_sc->temperature);
  result.records.push_back(chat_call);
  result.reply = chat_call.api_content.empty()
                     ? "任务已完成。\n" + epr.final_context
                     : chat_call.api_content;

  for (auto &rec : result.records) {
    result.total_tokens.prompt_tokens += rec.tokens.prompt_tokens;
    result.total_tokens.completion_tokens += rec.tokens.completion_tokens;
    result.total_tokens.total_tokens += rec.tokens.total_tokens;
  }
  return result;
}

// ===================================================================
// PLAN mode: plan_interact()
// ===================================================================
AgentResult Agent::plan_interact(const std::string &input) {
  AgentResult result;
  log_("[PLAN_MODE] plan_dispatcher");

  auto *pd_sc = loader_.get("plan_dispatcher");
  if (!pd_sc) {
    log_("[ERROR] plan_dispatcher not found");
    result.reply = "规划调度器不可用。请确保 plan_dispatcher.json 已配置。";
    return result;
  }

  std::string pd_prompt = pd_sc->prompt;
  std::string registry = loader_.build_skill_registry();
  if (!registry.empty()) {
    pd_prompt += "\n\n【当前已注册的所有可用专家】\n" + registry;
  }
  if (!plan_context_history_.empty()) {
    pd_prompt += "\n\n【规划对话历史】\n" + plan_context_history_ + "\n";
    pd_prompt += "请基于以上对话历史继续与用户交流或给出最终规划。";
  }

  auto pd_call =
      make_call("plan_dispatcher", pd_prompt, input, pd_sc->temperature);
  result.records.push_back(pd_call);
  std::string pd_output = pd_call.api_content;
  if (pd_output.empty()) {
    log_("[ERROR] plan_dispatcher returned empty");
    result.reply = "规划调度器返回为空。";
    return result;
  }

  std::string chat_content;
  if (XmlProtocol::has_chat(pd_output, chat_content)) {
    plan_context_history_ += "用户: " + input + "\n";
    plan_context_history_ += "规划器: " + chat_content + "\n";
    const size_t MAX_PLAN_CTX = 3000;
    if (plan_context_history_.size() > MAX_PLAN_CTX)
      plan_context_history_ = plan_context_history_.substr(
          plan_context_history_.size() - MAX_PLAN_CTX);
    log_("[PLAN_CHAT] " + chat_content);
    auto chat_call = make_call("chat", loader_.get("chat")->prompt,
                               "[规划对话]\n" + chat_content,
                               loader_.get("chat")->temperature);
    result.records.push_back(chat_call);
    result.reply =
        chat_call.api_content.empty() ? chat_content : chat_call.api_content;
    return result;
  }

  std::string marker_msg;
  int marker = XmlProtocol::has_final_marker(pd_output, marker_msg);
  if (marker == 1) {
    pending_outline_ = marker_msg;
    plan_context_history_.clear();
    log_("[PLAN_DONE] " + pending_outline_);
    result.reply = "✅ 已确定任务大纲。输入 /act 开始执行。\n\n任务大纲：\n" +
                   pending_outline_;
    return result;
  }
  if (marker == 2) {
    log_("[PLAN_FAIL] " + marker_msg);
    result.reply = "规划失败：" + marker_msg;
    return result;
  }
  result.reply = pd_output;
  return result;
}

// ===================================================================
// Execute outline (from PLAN mode transition)
// ===================================================================
AgentResult Agent::execute_outline(const std::string &task_outline,
                                   const std::string &conversation_history) {
  AgentResult result;
  if (task_outline.empty()) {
    result.reply = "没有待执行的任务大纲。请先使用 /plan 进行规划。";
    return result;
  }
  log_("[EXEC_OUTLINE] Routing to dispatcher");

  auto *disp_sc = loader_.get("dispatcher");
  if (!disp_sc) {
    log_("[ERROR] dispatcher not found");
    result.reply = "任务执行器不可用。请确保 dispatcher.json 已配置。";
    return result;
  }

  std::string disp_prompt = disp_sc->prompt;
  std::string registry = loader_.build_skill_registry();
  if (!registry.empty()) {
    disp_prompt += "\n\n【当前已注册的所有可用专家】\n" + registry;
  }
  std::string disp_input =
      "以下是经过规划后确定的任务大纲，请据此生成可执行计划并执行：\n\n" +
      task_outline;
  if (!conversation_history.empty()) {
    std::string recent = conversation_history;
    const size_t MAX_RECENT = 1500;
    if (recent.size() > MAX_RECENT)
      recent = recent.substr(recent.size() - MAX_RECENT);
    disp_prompt += "\n\n【近期对话参考】\n" + recent + "\n";
  }

  auto plan_call =
      make_call("dispatcher", disp_prompt, disp_input, disp_sc->temperature);
  result.records.push_back(plan_call);
  std::string plan_output = plan_call.api_content;
  if (plan_output.empty()) {
    log_("[ERROR] Dispatcher returned empty");
    result.reply = "任务调度失败。";
    return result;
  }

  std::string main_context = disp_input;

  PlanResult plan;
  if (!XmlProtocol::parse_plan(plan_output, plan)) {
    std::string chat_content;
    if (XmlProtocol::has_chat(plan_output, chat_content)) {
      log_("[CHAT] Dispatcher routed to chat");
      auto chat_call = make_call("chat", loader_.get("chat")->prompt,
                                 "[任务大纲执行]\n" + chat_content,
                                 loader_.get("chat")->temperature);
      result.records.push_back(chat_call);
      result.reply = chat_call.api_content;
      return result;
    }
    log_("[WARN] No plan from dispatcher");
    result.reply = plan_output;
    return result;
  }

  ExecutePlanResult epr = execute_plan(plan, 0, main_context, {}, 0);

  auto *chat_sc = loader_.get("chat");
  if (!chat_sc) {
    result.reply = "任务执行结果:\n" + epr.final_context;
    return result;
  }

  std::string chat_extra;
  chat_extra += "[已完成的任务]\n";
  for (auto &tr : epr.task_results)
    chat_extra += "- " + tr.skill + ": " + tr.summary +
                  (tr.succeeded ? "" : " (失败)") + "\n";
  chat_extra += "[CONTEXT: " + epr.final_context + "]\n";
  chat_extra += "[任务大纲: " + task_outline + "]";
  if (epr.was_resumed)
    chat_extra += "\n[系统提示] 任务曾在失败后自动恢复重试。";

  log_("[CHAT] Summarizing outline execution");
  auto chat_call =
      make_call("chat", chat_sc->prompt, chat_extra, chat_sc->temperature);
  result.records.push_back(chat_call);
  result.reply = chat_call.api_content.empty()
                     ? "任务执行结果:\n" + epr.final_context
                     : chat_call.api_content;

  for (auto &rec : result.records) {
    result.total_tokens.prompt_tokens += rec.tokens.prompt_tokens;
    result.total_tokens.completion_tokens += rec.tokens.completion_tokens;
    result.total_tokens.total_tokens += rec.tokens.total_tokens;
  }
  return result;
}

// ===================================================================
// Execute resume (from /resume CLI)
// ===================================================================
AgentResult Agent::execute_resume(const std::string &resume_context,
                                  int start_from) {
  AgentResult result;
  if (task_context_.empty()) {
    result.reply = "没有可恢复的任务历史。";
    return result;
  }
  if (start_from < 0 || start_from >= (int)task_context_.size()) {
    result.reply = "无效的恢复点索引。使用 /resume <index> 指定。可用索引: 0-" +
                   std::to_string((int)task_context_.size() - 1);
    return result;
  }

  log_("[RESUME] Starting from task_context_[" + std::to_string(start_from) +
       "]");

  auto *disp_sc = loader_.get("dispatcher");
  if (!disp_sc) {
    log_("[ERROR] dispatcher not found");
    result.reply = "任务执行器不可用。";
    return result;
  }

  std::string resume_input =
      "这是之前执行中断的任务历史。请分析进度并继续执行。\n\n";
  resume_input += "=== 已完成的任务 ===\n";
  for (int i = 0; i < start_from; i++) {
    auto &e = task_context_[i];
    resume_input +=
        "[" + std::to_string(i) + "] " + e.skill + ": " + e.task + "\n";
    resume_input +=
        "  结果: " + (e.succeeded ? std::string("OK ") : std::string("FAIL ")) +
        e.summary + "\n";
  }
  resume_input += "\n=== 需要继续执行的任务 ===\n";
  for (int i = start_from; i < (int)task_context_.size(); i++) {
    auto &e = task_context_[i];
    resume_input +=
        "[" + std::to_string(i) + "] " + e.skill + ": " + e.task + "\n";
    resume_input += "  原结果: " +
                    (e.succeeded ? std::string("OK ") : std::string("FAIL ")) +
                    e.summary + "\n";
  }

  resume_input += "\n请根据以上信息，输出新的<plan>来继续执行中断的任务。"
                  "\n可以跳过已成功完成的部分，专注于失败或未完成的部分。\n";

  auto plan_call = make_call("dispatcher", disp_sc->prompt, resume_input,
                             disp_sc->temperature);
  result.records.push_back(plan_call);
  std::string plan_output = plan_call.api_content;
  if (plan_output.empty()) {
    result.reply = "调度器返回为空，无法恢复。";
    return result;
  }

  PlanResult plan;
  if (!XmlProtocol::parse_plan(plan_output, plan)) {
    result.reply = plan_output;
    return result;
  }

  std::string main_context = resume_input;
  std::vector<TaskResult> initial_results;
  for (int i = 0; i < start_from; i++) {
    auto &e = task_context_[i];
    initial_results.push_back({e.skill, e.summary, e.succeeded});
    if (!e.full_log.empty()) {
      main_context += "\n[" + e.skill + " 日志]: " + e.full_log.substr(0, 500);
    }
  }

  ExecutePlanResult epr =
      execute_plan(plan, 0, main_context, initial_results, 0);

  auto *chat_sc = loader_.get("chat");
  if (!chat_sc) {
    result.reply = "恢复执行结果:\n" + epr.final_context;
    return result;
  }

  std::string chat_extra;
  chat_extra += "[恢复执行的任务]\n";
  for (auto &tr : epr.task_results)
    chat_extra += "- " + tr.skill + ": " + tr.summary +
                  (tr.succeeded ? "" : " (失败)") + "\n";
  chat_extra += "[CONTEXT: " + epr.final_context + "]\n";
  chat_extra += "[恢复点: " + std::to_string(start_from) + "]";

  log_("[CHAT] Summarizing resume");
  auto chat_call =
      make_call("chat", chat_sc->prompt, chat_extra, chat_sc->temperature);
  result.records.push_back(chat_call);
  result.reply = chat_call.api_content.empty()
                     ? "恢复执行结果:\n" + epr.final_context
                     : chat_call.api_content;

  for (auto &rec : result.records) {
    result.total_tokens.prompt_tokens += rec.tokens.prompt_tokens;
    result.total_tokens.completion_tokens += rec.tokens.completion_tokens;
    result.total_tokens.total_tokens += rec.tokens.total_tokens;
  }
  return result;
}

} // namespace astral
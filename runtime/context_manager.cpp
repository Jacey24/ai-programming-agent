// context_manager.cpp — Manages conversation context and history for skills
#include "context_manager.hpp"

namespace astral {

ContextManager::ContextManager(int max_messages)
    : max_messages_(max_messages) {}

void ContextManager::add_message(const std::string &role,
                                 const std::string &content) {
  history_.push_back({role, content});
  while ((int)history_.size() > max_messages_)
    history_.pop_front();
}

std::vector<ContextMessage> ContextManager::recent() const {
  std::vector<ContextMessage> result;
  for (auto &msg : history_)
    result.push_back(msg);
  return result;
}

std::string
ContextManager::build_task_context(const std::string &task,
                                   const std::string &main_context,
                                   const std::vector<std::string> &results) {
  std::string ctx;
  ctx += "[TASK: " + task + "]\n";
  if (!results.empty()) {
    ctx += "[已完成步骤]\n";
    for (auto &r : results)
      ctx += "- " + r + "\n";
  }
  ctx += "[CONTEXT: " + main_context + "]\n";
  return ctx;
}

std::string ContextManager::build_summary_context(
    const std::string &original_input, const std::string &main_context,
    const std::vector<std::pair<std::string, std::string>> &task_results) {
  std::string ctx;
  ctx += "[已完成的任务]\n";
  for (auto &tr : task_results)
    ctx += "- " + tr.first + ": " + tr.second + "\n";
  ctx += "[CONTEXT: " + main_context + "]\n";
  ctx += "[用户原话: " + original_input + "]";
  return ctx;
}

void ContextManager::clear() { history_.clear(); }

} // namespace astral
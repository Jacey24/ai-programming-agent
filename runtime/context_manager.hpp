// context_manager.hpp — Manages conversation context and history for skills
#pragma once
#include <deque>
#include <string>
#include <vector>

namespace astral {

// Represents a single message in the conversation history
struct ContextMessage {
  std::string role; // "system", "user", "assistant"
  std::string content;
};

// Manages context for a skill execution session
// Handles: context windowing, history accumulation, context summary
class ContextManager {
public:
  ContextManager(int max_messages = 30);

  // Add a message to history
  void add_message(const std::string &role, const std::string &content);

  // Get recent messages within the window limit
  std::vector<ContextMessage> recent() const;

  // Build a context string from task results and main context
  static std::string
  build_task_context(const std::string &task, const std::string &main_context,
                     const std::vector<std::string> &results);

  // Build chat summary context
  static std::string build_summary_context(
      const std::string &original_input, const std::string &main_context,
      const std::vector<std::pair<std::string, std::string>> &task_results);

  // Clear history
  void clear();

private:
  std::deque<ContextMessage> history_;
  int max_messages_;
};

} // namespace astral
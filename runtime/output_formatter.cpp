// output_formatter.cpp — Debug-level output formatting for the Astral REPL
#include "output_formatter.hpp"
#include "../core/util.hpp"
#include <algorithm>
#include <sstream>

namespace astral {

bool OutputFormatter::should_show_log(const std::string &msg,
                                      DebugLevel level) {
  // DEBUG_NONE 0: only show essential flow markers
  if (level == DEBUG_NONE) {
    // Show: PLAN, TASK, DONE, ERROR, CHAT — hide CMD, RESULT
    if (msg.rfind("[CMDP]", 0) == 0)
      return false; // [CMDP] only shown in higher levels
    if (msg.rfind("[RESULT]", 0) == 0)
      return false;
    if (msg.rfind("[CMD]", 0) == 0)
      return false;
    return true;
  }

  // DEBUG_NORMAL 1: show everything except raw API dumps
  if (level == DEBUG_NORMAL) {
    return true;
  }

  // DEBUG_VERBOSE 2: everything visible
  return true;
}

std::string OutputFormatter::format_log(const std::string &msg,
                                        DebugLevel level) {
  if (!should_show_log(msg, level))
    return "";
  return "  " + msg + "\n";
}

std::string OutputFormatter::format_token_summary(TokenUsage usage) {
  if (usage.total_tokens == 0)
    return "";
  std::string s;
  s += "  [TOKENS] 提示 " + std::to_string(usage.prompt_tokens) + " | 生成 " +
       std::to_string(usage.completion_tokens) + " | 总计 " +
       std::to_string(usage.total_tokens) + "\n";
  return s;
}

std::string
OutputFormatter::format_response(const std::string &chat_reply,
                                 const std::vector<CallRecord> &records,
                                 TokenUsage total_tokens, DebugLevel level) {

  std::string output;

  // DEBUG_NONE 0: just the chat reply, nothing else
  if (level == DEBUG_NONE) {
    return chat_reply;
  }

  // DEBUG_NORMAL 1: chat reply + call breakdown + token summary
  if (level == DEBUG_NORMAL) {
    output += chat_reply;

    // Show call records
    if (!records.empty()) {
      output += "\n\n--- 调用详情 ---\n";
      for (auto &rec : records) {
        output += "  [" + rec.skill + "] ";
        // Show a preview of the content (first 80 chars)
        std::string preview = rec.api_content;
        if (preview.size() > 80)
          preview = preview.substr(0, 80) + "...";
        // Remove newlines
        for (size_t i = 0; i < preview.size(); i++)
          if (preview[i] == '\n')
            preview[i] = ' ';
        output += preview + "\n";
        if (rec.tokens.total_tokens > 0) {
          output += "     tokens: " + std::to_string(rec.tokens.prompt_tokens) +
                    "→" + std::to_string(rec.tokens.completion_tokens) +
                    " (总计 " + std::to_string(rec.tokens.total_tokens) + ")\n";
        }
      }
    }

    // Token summary at end
    std::string ts = format_token_summary(total_tokens);
    if (!ts.empty())
      output += "\n" + ts;

    return output;
  }

  // DEBUG_VERBOSE 2: include full raw API responses
  if (level == DEBUG_VERBOSE) {
    output += chat_reply;

    if (!records.empty()) {
      output += "\n\n=== 完整调用记录 ===\n";
      for (size_t i = 0; i < records.size(); i++) {
        auto &rec = records[i];
        output += "--- Call #" + std::to_string(i + 1) + ": [" + rec.skill +
                  "] ---\n";
        output += "Content: " + rec.api_content + "\n";
        output += "Raw: " + rec.raw_response + "\n\n";
      }
    }

    return output;
  }

  return chat_reply;
}

} // namespace astral
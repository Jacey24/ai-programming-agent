// output_formatter.hpp — Debug-level output formatting for the Astral REPL
#pragma once
#include <string>
#include <vector>

namespace astral {

// Debug levels for output control
enum DebugLevel {
  DEBUG_NONE = 0,    // Only show task flow (PLAN/TASK/DONE/ERROR/CHAT)
  DEBUG_NORMAL = 1,  // Show detailed info + token consumption
  DEBUG_VERBOSE = 2, // Show full raw API responses
};

// Extracted token usage from API response
struct TokenUsage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
};

// A single API call record
struct CallRecord {
  std::string skill;
  std::string api_content; // parsed AI response text
  TokenUsage tokens;
  std::string raw_response; // full raw JSON for DEBUG_VERBOSE
};

// Formats and filters output based on debug level
class OutputFormatter {
public:
  // Check if a log message should be displayed at the given debug level
  // Returns the formatted message or empty string to suppress
  static std::string format_log(const std::string &msg, DebugLevel level);

  // Format the final agent response with optional call records and token stats
  static std::string format_response(const std::string &chat_reply,
                                     const std::vector<CallRecord> &records,
                                     TokenUsage total_tokens, DebugLevel level);

  // Format token usage summary
  static std::string format_token_summary(TokenUsage usage);

private:
  // Check log prefix visibility
  static bool should_show_log(const std::string &msg, DebugLevel level);
};

} // namespace astral
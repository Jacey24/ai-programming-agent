// xml_protocol.hpp — Protocol definitions for skill-agent communication
// Defines the XML-based protocol used between Dispatcher, Experts, and Agent
#pragma once
#include <string>
#include <vector>

namespace astral {

// A single task item from a <plan> block
struct TaskItem {
  std::string skill;     // target expert name
  std::string task;      // task description
  bool fallback = false; // only executed if previous failed
};

// Parsed plan result
struct PlanResult {
  std::vector<TaskItem> tasks;
};

// XML Protocol parser
// Handles: <plan>, <cmd>, <chat>, DONE, FAIL tags
class XmlProtocol {
public:
  // Parse a <plan> block from AI output
  // Returns true if plan found and parsed successfully
  static bool parse_plan(const std::string &output, PlanResult &plan);

  // Parse a <cmd> block from expert output
  // Returns true if command found
  static bool parse_cmd(const std::string &output, std::string &cmd);

  // Detect DONE marker and extract summary
  // Returns true if DONE found
  static bool has_done(const std::string &output, std::string &summary);

  // Detect <chat> block for direct dispatcher-to-chat routing
  // Returns true and extracts inner content if found
  static bool has_chat(const std::string &output, std::string &content);

  // Detect final marker (DONE or FAIL) and extract message
  // Returns: 0 = no marker, 1 = DONE, 2 = FAIL
  // Sets message to the text after DONE or FAIL
  static int has_final_marker(const std::string &output, std::string &message);
};

} // namespace astral
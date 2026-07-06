// agent_types.hpp — Data structures and enums for the Agent system
// Split from agent.hpp/agent.cpp to improve module separation
#pragma once
#include "../runtime/output_formatter.hpp"
#include <map>
#include <string>
#include <vector>


namespace astral {

// Per-task execution result summary
struct TaskResult {
  std::string skill;
  std::string summary; // DONE/FAIL summary from the expert
  bool succeeded;
};

// Per-task execution log entry (for analyser resume system)
struct PlanEntry {
  int plan_index; // index in task_context_
  int task_index; // index within the plan
  std::string skill;
  std::string task;     // original task description
  std::string full_log; // complete execution log (AI outputs + cmd results)
  std::string summary;  // final DONE/FAIL summary
  bool succeeded;
};

struct AgentConfig {
  std::string api_key;
  std::string api_url;
  std::string model;
  int max_tokens = 4096;
  int max_rounds = 10;
};

// Internal result of executing a plan (not exposed outside Agent)
struct ExecutePlanResult {
  bool all_succeeded = true;
  bool was_resumed = false;
  std::string final_context;
  std::vector<TaskResult> task_results;
  std::vector<std::string> flat_cmd_results;
};

// Result of a full agent.process() call
struct AgentResult {
  std::string reply;
  std::vector<CallRecord> records;
  TokenUsage total_tokens;
};

} // namespace astral
// agent.hpp — AI skill dispatch engine with task planning
// After module split: struct definitions moved to agent_types.hpp
#pragma once
#include "../runtime/output_formatter.hpp"
#include "../runtime/xml_protocol.hpp"
#include "agent_types.hpp"
#include "shell.hpp"
#include "skill_loader.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace astral {

class ApiClient; // forward declaration
class Agent {
public:
  Agent(Shell &shell, SkillLoader &loader, const AgentConfig &cfg);

  void set_logger(std::function<void(const std::string &)> log);

  // Set confirmer callback for dangerous commands
  // Called when a registered dangerous command is about to be executed.
  void set_confirmer(
      std::function<bool(const std::string &skill, std::string &cmd_line)>
          confirmer);

  // Process natural language input through the task planning + execution chain
  AgentResult process(const std::string &input,
                      const std::string &conversation_history = "");

  // Interact with plan_dispatcher for multi-turn planning mode.
  AgentResult plan_interact(const std::string &input);

  // Execute a task outline (compressed plan from plan_dispatcher)
  AgentResult execute_outline(const std::string &task_outline,
                              const std::string &conversation_history = "");

  // Execute a resume from a specific point in task_context_
  AgentResult execute_resume(const std::string &resume_context, int start_from);

  // Get task_context_ (for /resume CLI to inspect)
  const std::vector<PlanEntry> &task_context() const { return task_context_; }
  const std::string build_task_context_summary() const;

  // Check if there is a pending DONE summary from plan_dispatcher
  bool has_pending_outline() const { return !pending_outline_.empty(); }
  const std::string &pending_outline() const { return pending_outline_; }
  void clear_pending_outline() { pending_outline_.clear(); }

  // ========== Workfolder (/cd) system ==========
  // The workfolder is set by the user and AI has no authority to change it.
  // It is injected into every expert's context as [????].
  void set_workfolder(const std::string &path);
  const std::string &workfolder() const { return workfolder_; }
  std::vector<std::string> workfolder_history() const;

  // ========== Home folder (/home) system ==========
  // /cd ~ returns to this folder. Persisted in workfolder_history.json.
  void set_home_folder(const std::string &path);
  const std::string &home_folder() const { return home_folder_; }

  // Mask action: three states for each dangerous command
  enum MaskAction { MASK_NORMAL = 0, MASK_BLOCK = 1, MASK_APPROVE = 2 };

  // ========== Folder policy masks (persisted to cmd_masks.json) ==========
  // /block, /approve, /unblock, /setmask — writes to JSON file in workfolder.
  // These take priority over memory masks.
  void set_mask(const std::string &cmd, MaskAction action);
  void block_cmd(const std::string &cmd) { set_mask(cmd, MASK_BLOCK); }
  void approve_cmd(const std::string &cmd) { set_mask(cmd, MASK_APPROVE); }
  void unblock_cmd(const std::string &cmd) { set_mask(cmd, MASK_NORMAL); }

  // ========== Memory masks (default settings, never written to file)
  // ========== /mask CMD approve/block/normal — in-memory only. Used as
  // fallback when no folder policy exists for this command.
  void set_memory_mask(const std::string &cmd, MaskAction action);

  // ========== Combined mask query ==========
  // Priority: folder policy (from cmd_masks.json) > memory mask > NORMAL
  MaskAction get_mask(const std::string &cmd) const;

  // Show current combined mask status for all known dangerous commands
  // or for a specific command if cmd is non-empty.
  std::string mask_status(const std::string &cmd = "") const;
  bool has_masks() const {
    return !mask_map_.empty() || !memory_mask_map_.empty();
  }

  // Load/save workfolder + home state from/to workfolder_history.json
  void load_workfolder_state();
  void save_workfolder_state() const;

private:
  Shell &shell_;
  SkillLoader &loader_;
  AgentConfig cfg_;
  std::function<void(const std::string &)> log_ = [](auto &) {};
  std::function<bool(const std::string &skill, std::string &cmd_line)>
      confirmer_;
  ApiClient *api_ = nullptr; // lazily created

  // Multi-turn plan_dispatcher context
  std::string plan_context_history_;

  // Last DONE summary from plan_dispatcher
  std::string pending_outline_;

  // ========== Workfolder & Home ==========
  std::string workfolder_;
  std::string home_folder_;

  // Folder policy masks — persisted to cmd_masks.json
  std::map<std::string, MaskAction> mask_map_;

  // Memory masks — never written to file, used as default fallback
  std::map<std::string, MaskAction> memory_mask_map_;

  // Recent workfolder history (most recent first, max ~10)
  std::vector<std::string> wf_history_;

  static constexpr int MAX_WF_HISTORY = 10;

  // ==================== Task Context (analyser/resume system)
  std::vector<PlanEntry> task_context_;
  int plan_counter_ = 0;
  static constexpr int MAX_RESUME_DEPTH = 3;

  // ==================== Core execution engine ====================
  ExecutePlanResult execute_plan(const PlanResult &plan, int start_index,
                                 const std::string &initial_context,
                                 const std::vector<TaskResult> &initial_results,
                                 int resume_depth);

  bool invoke_analyser(const std::string &fail_context,
                       const std::string &full_task_log,
                       const std::string &task_summary, PlanResult &out_plan,
                       CallRecord &out_call_record);

  // ==================== Private helpers ====================
  void load_masks();
  void save_masks() const;
  std::string mask_path() const;
  void save_workfolder_history() const;
  void load_workfolder_history();
  std::string wf_history_path() const;

  ApiClient &api();
  std::string exec_cmd(const std::string &cmd_line);
  std::string build_expert_context(const std::string &task,
                                   const std::string &main_context,
                                   const std::vector<TaskResult> &results,
                                   bool isolated,
                                   const std::vector<std::string> &cmd_results);
  CallRecord make_call(const std::string &skill_name,
                       const std::string &system_prompt,
                       const std::string &user_input, double temperature);
};

} // namespace astral
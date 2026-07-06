// Astral — Skill-driven agent system (CLI entry point)
// Build (after module split): cl /EHsc /std:c++20 source\main.cpp
//   core\agent_policy.cpp core\agent_exec.cpp core\agent_interact.cpp
//   core\cli_builtins.cpp core\shell.cpp core\skill_loader.cpp
//   runtime\api_client.cpp runtime\context_manager.cpp
//   runtime\output_formatter.cpp runtime\xml_protocol.cpp
//   /Fe:Astral.exe
#include "core/agent.hpp"
#include "core/agent_types.hpp"
#include "core/cli_builtins.hpp"
#include "core/shell.hpp"
#include "core/skill_loader.hpp"
#include "runtime/output_formatter.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace astral;

// Two interaction modes:
// - ACT mode (default): dispatcher plans + executes immediately
// - PLAN mode: plan_dispatcher converses with user, accumulates context,
//   outputs compressed task outline; user switches to ACT to execute.
enum class AppMode { ACT, PLAN };

int main(int argc, char *argv[]) {
  // Set console to UTF-8
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  // Parse CLI args
  std::string mode = "console";
  int port = 8080;
  DebugLevel debug_level = DEBUG_NONE;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--web")
      mode = "web";
    else if (arg == "--port" && i + 1 < argc)
      port = std::stoi(argv[++i]);
  }

  // Load API key
  std::string api_key;
  std::ifstream kf("api_key.txt");
  if (kf.is_open()) {
    std::getline(kf, api_key);
    kf.close();
  }
  if (api_key.empty()) {
    std::cout << "Enter API Key: ";
    std::getline(std::cin, api_key);
    if (api_key.empty()) {
      std::cout << "No API key. Exiting.\n";
      return 1;
    }
  }

  // Initialize Shell
  Shell shell;

  // Load skills from skills/ directory — this registers skill commands
  SkillLoader loader;
  int skill_count = loader.load_all("skills", shell);
  std::cout << "[OK] Loaded " << skill_count << " skills / "
            << shell.list_commands().size() << " commands\n";

  // Initialize Agent
  AgentConfig cfg;
  cfg.api_key = api_key;
  cfg.api_url = "https://api.deepseek.com/v1";
  cfg.model = "deepseek-chat";

  Agent agent(shell, loader, cfg);
  agent.set_logger([&debug_level](const std::string &msg) {
    std::string formatted = OutputFormatter::format_log(msg, debug_level);
    if (!formatted.empty())
      std::cout << formatted;
  });

  // Load workfolder state from previous session
  agent.load_workfolder_state();

  // Set confirmer for dangerous commands
  agent.set_confirmer(
      [](const std::string &skill, std::string &cmd_line) -> bool {
        std::cout << "\n⚠️  [危险操作确认] 专家: " << skill << "\n";
        std::cout << "   命令: " << cmd_line << "\n\n";
        std::cout << "  [y] 批准执行  [n] 拒绝  [e] 编辑命令后执行\n";
        std::cout << "  > ";
        std::string input;
        std::getline(std::cin, input);
        if (input.empty())
          input = "n";
        char choice = std::tolower(input[0]);
        if (choice == 'y') {
          std::cout << "  ✓ 已批准\n\n";
          return true;
        } else if (choice == 'e') {
          std::cout << "  请输入修改后的完整命令:\n  > ";
          std::string edited;
          std::getline(std::cin, edited);
          if (!edited.empty()) {
            cmd_line = edited;
            std::cout << "  ✓ 已编辑并执行\n\n";
            return true;
          }
          std::cout << "  ✗ 编辑为空，已取消\n\n";
          return false;
        } else {
          std::cout << "  ✗ 已拒绝\n\n";
          return false;
        }
      });

  // =========================================================================
  // Register builtin CLI commands
  // =========================================================================
  // Most commands are registered via cli_builtins.cpp (cli::register_all).
  // Mode-specific commands (/plan, /act, /debug) are registered here because
  // they capture main() local variables (app_mode, debug_level).

  // Register general CLI builtins from cli_builtins.cpp
  cli::register_all(shell, agent);

  // Mode state
  AppMode app_mode = AppMode::ACT;

  // /plan — switch to PLAN mode
  shell.register_builtin(
      "plan",
      [&app_mode](const std::string &, ShellResult &r) -> bool {
        if (app_mode == AppMode::PLAN) {
          r.stdout_text = "已在规划模式。输入 /act 切换到执行模式。";
        } else {
          app_mode = AppMode::PLAN;
          r.stdout_text =
              "已切换到规划模式。输入需求细节，规划调度器将与你对话完善任务。\n"
              "输入 /act 完成规划并开始执行。";
        }
        r.ok = true;
        return true;
      },
      false, "切换到规划模式（多轮对话收集需求）");

  // /act — switch to ACT mode
  shell.register_builtin(
      "act",
      [&app_mode, &agent](const std::string &args, ShellResult &r) -> bool {
        if (app_mode == AppMode::ACT) {
          r.stdout_text = "已在执行模式。";
          r.ok = true;
          return true;
        }
        app_mode = AppMode::ACT;
        if (agent.has_pending_outline()) {
          std::string outline = agent.pending_outline();
          agent.clear_pending_outline();
          r.stdout_text =
              "正在执行已规划的任务大纲...\n（任务结果将在此处显示）\n" +
              outline;
        } else {
          r.stdout_text = "已切换到执行模式。直接输入指令即可执行。";
        }
        r.ok = true;
        return true;
      },
      false, "切换到执行模式（直接执行/继续执行待处理计划）");

  // /debug — set debug level
  shell.register_builtin(
      "debug",
      [&debug_level](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = "当前调试级别: " + std::to_string((int)debug_level);
          r.ok = true;
          return true;
        }
        try {
          int level = std::stoi(args);
          if (level < 0)
            level = 0;
          if (level > 2)
            level = 2;
          debug_level = (DebugLevel)level;
          r.stdout_text = "调试级别已设为 " + std::to_string((int)debug_level);
          r.ok = true;
        } catch (...) {
          r.stdout_text = "用法: /debug 0|1|2";
          r.ok = false;
        }
        return true;
      },
      false, "设置调试级别: 0=简洁 1=详细 2=完整原始输出");

  // =========================================================================
  // Startup banner
  // =========================================================================
  std::cout << "\n=== Astral Ready ===\n";
  std::cout << "Natural language input -> AI skill chain\n";
  std::cout << "输入 /help 查看所有可用命令\n";
  std::cout << "输入 /ls 列出技能指令\n\n";

  // Cross-round conversation history for dispatcher context continuity
  std::string conversation_history;
  const size_t MAX_HISTORY = 2000;

  // REPL loop
  std::string line;
  while (true) {
    // Show mode-aware prompt
    if (app_mode == AppMode::PLAN)
      std::cout << "[PLAN] > ";
    else
      std::cout << "> ";

    if (!std::getline(std::cin, line))
      break;
    if (line.empty())
      continue;
    if (line == "exit" || line == "quit")
      break;

    // =================================================================
    // All /-prefixed commands go through the unified shell router
    // =================================================================
    if (line[0] == '/') {
      std::string cmd_line = line.substr(1);
      if (cmd_line.empty()) {
        std::cout << "空的命令。输入 /help 查看可用命令。\n";
        continue;
      }

      auto result = shell.run(cmd_line);
      if (result.ok) {
        std::cout << result.stdout_text;
        if (!result.stdout_text.empty() && result.stdout_text.back() != '\n')
          std::cout << "\n";
      } else {
        std::cout << "错误: " << result.stderr_text << "\n";
      }
      continue;
    }

    // =================================================================
    // Natural language input -> AI processing
    // =================================================================
    AgentResult ar;

    if (app_mode == AppMode::PLAN) {
      ar = agent.plan_interact(line);
    } else {
      ar = agent.process(line, conversation_history);
    }

    std::string output = OutputFormatter::format_response(
        ar.reply, ar.records, ar.total_tokens, debug_level);
    std::cout << output << "\n";

    // Update conversation history
    if (!ar.reply.empty()) {
      conversation_history += "用户: " + line + "\n";
      conversation_history += "助手: " + ar.reply + "\n";
      if (conversation_history.size() > MAX_HISTORY)
        conversation_history = conversation_history.substr(
            conversation_history.size() - MAX_HISTORY);
    }
  }

  std::cout << "Goodbye.\n";
  return 0;
}
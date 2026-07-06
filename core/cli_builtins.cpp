// cli_builtins.cpp — CLI builtin command registrations for Astral REPL
// Extracted from main.cpp to separate command definitions from boot logic.
// [配合关系]
//   输出到 main.cpp: 每个注册函数都是接收 Shell&, Agent& 等引用并调用
//   shell.register_builtin() 的纯函数。
//   依赖 agent.hpp (mask_status, set_memory_mask, approve_cmd 等)
//   依赖 shell.hpp (register_builtin, help_text, list_commands 等)
#include "agent.hpp"
#include "agent_types.hpp"
#include "shell.hpp"
#include <algorithm>
#include <sstream>

namespace astral {
namespace cli {

// Helper: register all CLI builtins on a given Shell and Agent
// Called from main() during initialization.
void register_all(Shell &shell, Agent &agent) {

  // /help — show all available commands
  shell.register_builtin(
      "help",
      [&shell, &agent](const std::string &, ShellResult &r) -> bool {
        std::string text = shell.help_text();
        r.ok = true;
        r.stdout_text = text;
        return true;
      },
      false, "显示所有可用命令的帮助信息");

  // /ls — list commands (alias for /help)
  shell.register_builtin(
      "ls",
      [&shell, &agent](const std::string &args, ShellResult &r) -> bool {
        if (!args.empty()) {
          std::string upper = args;
          std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
          if (shell.has_cmd(upper)) {
            r.stdout_text = upper + " — " + shell.cmd_description(upper) +
                            (shell.is_dangerous(upper) ? " ⚠️危险" : "");
            r.ok = true;
            return true;
          }
          r.stdout_text = "未知命令: " + args;
          r.ok = false;
          return true;
        }
        auto cmds = shell.list_commands();
        r.stdout_text = "可用命令 (" + std::to_string(cmds.size()) + " 个):\n";
        for (auto &c : cmds) {
          r.stdout_text += "  " + c;
          if (shell.is_dangerous(c))
            r.stdout_text += " ⚠️";
          r.stdout_text += "\n";
        }
        r.ok = true;
        return true;
      },
      false, "列出所有可用命令，或 /ls CMD 查看某个命令的详情");

  // NOTE: /plan, /act, /debug are NOT registered here.
  // They are registered in main.cpp because they capture main() local
  // variables (app_mode, debug_level). Registering them here would
  // cause duplicate registration conflicts.

  // /cd — change workfolder
  shell.register_builtin(
      "cd",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          if (agent.workfolder().empty())
            r.stdout_text = "当前未设置工作目录。使用 /cd <路径> 设置。";
          else
            r.stdout_text = "当前工作目录: " + agent.workfolder();
          r.ok = true;
          return true;
        }

        if (args == "..") {
          std::string cur = agent.workfolder();
          if (cur.empty()) {
            r.stdout_text = "当前没有工作目录，无法返回上级。";
            r.ok = false;
            return true;
          }
          if (!cur.empty() && cur.back() == '\\')
            cur.pop_back();
          auto pos = cur.rfind('\\');
          if (pos == std::string::npos) {
            r.stdout_text = "已是根目录，无法返回上级。";
            r.ok = false;
            return true;
          }
          std::string parent = cur.substr(0, pos + 1);
          agent.set_workfolder(parent);
          r.stdout_text = "✓ 已切换到上级目录: " + agent.workfolder();
          r.ok = true;
          return true;
        }

        if (args == "~") {
          if (agent.home_folder().empty()) {
            r.stdout_text = "尚未设置 Home 文件夹。使用 /home <路径> 设置。";
            r.ok = false;
            return true;
          }
          agent.set_workfolder(agent.home_folder());
          r.stdout_text = "✓ 已切换到 Home 文件夹: " + agent.workfolder();
          r.ok = true;
          return true;
        }

        agent.set_workfolder(args);
        r.stdout_text = "✓ 工作目录已设置为: " + agent.workfolder() +
                        "\n此目录将以 [工作目录] 注入到所有AI专家上下文中。\n"
                        "AI 无权更改此设置。";
        auto hist = agent.workfolder_history();
        if (hist.size() > 1) {
          r.stdout_text += "\n\n最近使用的工作目录：\n";
          for (size_t i = 0; i < hist.size(); i++)
            r.stdout_text += "  " + std::to_string(i + 1) + ". " + hist[i] +
                             (i == 0 ? "  ← 当前" : "") + "\n";
        }
        r.ok = true;
        return true;
      },
      false,
      "切换工作目录（AI文件操作基准）:  .. 上级目录  ~ 回到Home  无参显示当前");

  // /home — set or show home folder
  shell.register_builtin(
      "home",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          if (agent.home_folder().empty())
            r.stdout_text = "尚未设置 Home 文件夹。使用 /home <路径> 设置。\n"
                            "设置后可使用 /cd ~ 快速返回。";
          else
            r.stdout_text = "Home 文件夹: " + agent.home_folder();
          r.ok = true;
          return true;
        }
        agent.set_home_folder(args);
        r.stdout_text = "✓ Home 文件夹已设置为: " + agent.home_folder() +
                        "\n使用 /cd ~ 快速切换到此目录。";
        r.ok = true;
        return true;
      },
      false, "设置或显示 Home 文件夹（/cd ~回到此处）");

  // /mask — show or set memory mask
  shell.register_builtin(
      "mask",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = agent.mask_status();
          r.ok = true;
          return true;
        }
        std::istringstream iss(args);
        std::string cmd_name, action_str;
        iss >> cmd_name;
        iss >> action_str;
        if (action_str.empty()) {
          r.stdout_text = agent.mask_status(cmd_name);
          r.ok = true;
          return true;
        }
        std::transform(action_str.begin(), action_str.end(), action_str.begin(),
                       ::tolower);
        Agent::MaskAction action;
        if (action_str == "approve")
          action = Agent::MASK_APPROVE;
        else if (action_str == "block")
          action = Agent::MASK_BLOCK;
        else if (action_str == "normal")
          action = Agent::MASK_NORMAL;
        else {
          r.stdout_text =
              "无效操作: " + action_str + "，请使用 approve/block/normal";
          r.ok = false;
          return true;
        }
        agent.set_memory_mask(cmd_name, action);
        std::string action_label = (action == Agent::MASK_BLOCK) ? "阻止"
                                   : (action == Agent::MASK_APPROVE)
                                       ? "自动批准"
                                       : "默认";
        r.stdout_text = "✓ 已设置内存掩码: " + cmd_name + " → " + action_label +
                        "（不写入文件夹策略文件）";
        r.ok = true;
        return true;
      },
      false,
      "查看/设置内存掩码: /mask 查看全部  /mask CMD 查看单个  /mask CMD "
      "approve|block|normal 设置");

  // /setmask — set folder policy mask
  shell.register_builtin(
      "setmask",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = "用法: /setmask <命令名> approve|block|normal\n"
                          "  例如: /setmask WRITE approve";
          r.ok = false;
          return true;
        }
        std::istringstream iss(args);
        std::string cmd_name, action_str;
        iss >> cmd_name;
        iss >> action_str;
        if (action_str.empty()) {
          r.stdout_text = "用法: /setmask <命令名> approve|block|normal";
          r.ok = false;
          return true;
        }
        std::transform(action_str.begin(), action_str.end(), action_str.begin(),
                       ::tolower);
        if (action_str == "approve") {
          agent.approve_cmd(cmd_name);
          r.stdout_text = "✓ 文件夹策略已设置: " + cmd_name +
                          " → 自动批准（已写入文件夹策略文件）";
        } else if (action_str == "block") {
          agent.block_cmd(cmd_name);
          r.stdout_text = "✓ 文件夹策略已设置: " + cmd_name +
                          " → 阻止（已写入文件夹策略文件）";
        } else if (action_str == "normal") {
          agent.unblock_cmd(cmd_name);
          r.stdout_text = "✓ 文件夹策略已清除: " + cmd_name +
                          "（已从文件夹策略文件中移除）";
        } else {
          r.stdout_text =
              "无效操作: " + action_str + "，请使用 approve/block/normal";
          r.ok = false;
          return true;
        }
        r.ok = true;
        return true;
      },
      false, "设置文件夹策略掩码（写入cmd_masks.json）: approve|block|normal");

  // /approve — shortcut for /setmask approve
  shell.register_builtin(
      "approve",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = "用法: /approve <命令名>  例如: /approve WRITE";
          r.ok = false;
          return true;
        }
        agent.approve_cmd(args);
        r.stdout_text = "✓ 文件夹策略已设置: " + args +
                        " → 自动批准（已写入文件夹策略文件）";
        r.ok = true;
        return true;
      },
      false, "设置文件夹策略为自动批准（/setmask CMD approve 的快捷命令）");

  // /block — shortcut for /setmask block
  shell.register_builtin(
      "block",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = "用法: /block <命令名>  例如: /block WRITE";
          r.ok = false;
          return true;
        }
        agent.block_cmd(args);
        r.stdout_text =
            "✓ 文件夹策略已设置: " + args + " → 阻止（已写入文件夹策略文件）";
        r.ok = true;
        return true;
      },
      false, "设置文件夹策略为阻止（/setmask CMD block 的快捷命令）");

  // /unblock — shortcut for /setmask normal
  shell.register_builtin(
      "unblock",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = "用法: /unblock <命令名>  例如: /unblock WRITE";
          r.ok = false;
          return true;
        }
        agent.unblock_cmd(args);
        r.stdout_text =
            "✓ 文件夹策略已清除: " + args + "（已从文件夹策略文件中移除）";
        r.ok = true;
        return true;
      },
      false, "清除文件夹策略（/setmask CMD normal 的快捷命令）");

  // /resume — show task history or resume from a specific point
  shell.register_builtin(
      "resume",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          r.stdout_text = agent.build_task_context_summary();
          r.ok = true;
          return true;
        }
        try {
          int idx = std::stoi(args);
          AgentResult ar = agent.execute_resume(std::string(), idx);
          r.stdout_text = ar.reply;
          r.ok = true;
          return true;
        } catch (...) {
          r.stdout_text = "用法: /resume [索引]\n  不指定索引则显示任务历史。";
          r.ok = false;
          return true;
        }
      },
      false, "从指定点恢复任务执行。不指定索引则显示历史。");

  // /workfolder — legacy alias for /cd
  shell.register_builtin(
      "workfolder",
      [&agent](const std::string &args, ShellResult &r) -> bool {
        if (args.empty()) {
          if (agent.workfolder().empty())
            r.stdout_text = "当前未设置工作目录。使用 /cd <路径> 设置。";
          else
            r.stdout_text = "当前工作目录: " + agent.workfolder();
          r.ok = true;
          return true;
        }
        agent.set_workfolder(args);
        r.stdout_text = "✓ 工作目录已设置为: " + agent.workfolder() +
                        "（提示: 将来可以直接使用 /cd 命令）";
        auto hist = agent.workfolder_history();
        if (hist.size() > 1) {
          r.stdout_text += "\n\n最近使用的工作目录：\n";
          for (size_t i = 0; i < hist.size(); i++)
            r.stdout_text += "  " + std::to_string(i + 1) + ". " + hist[i] +
                             (i == 0 ? "  ← 当前" : "") + "\n";
        }
        r.ok = true;
        return true;
      },
      false, "（旧命令）设置工作目录，现已被 /cd 替代");
}

} // namespace cli
} // namespace astral
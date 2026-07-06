// shell.hpp — Unified command routing for both AI skill commands and CLI
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace astral {

struct ShellResult {
  bool ok;
  int exit_code;
  std::string stdout_text;
  std::string stderr_text;
};

// Command registration: maps a command name to its handler and metadata
struct CmdRoute {
  std::string exe_path; // empty = builtin/internal handler
  bool dangerous = false;
  std::string description; // for /help display

  // For builtin/internal commands: handler function
  // Parameters: args_string, reference to result struct
  // Returns: true if handled
  std::function<bool(const std::string &args, ShellResult &out)> handler;
};

class Shell {
public:
  // Register an external skill executable command
  void register_cmd(const std::string &cmd, const std::string &exe_path,
                    bool dangerous = false,
                    const std::string &description = "");

  // Register a builtin/internal command handler (no external exe)
  void register_builtin(
      const std::string &cmd,
      std::function<bool(const std::string &args, ShellResult &out)> handler,
      bool dangerous = false, const std::string &description = "");

  // Execute a command line through the routing table
  // Builtin commands are handled internally; skill commands invoke the exe.
  ShellResult run(const std::string &cmd_line, int timeout_sec = 15);

  // Direct exe call (for special cases)
  ShellResult run_exe(const std::string &exe_path,
                      const std::vector<std::string> &args,
                      int timeout_sec = 15);

  // Query commands
  bool has_cmd(const std::string &cmd) const;
  bool is_dangerous(const std::string &cmd) const;
  std::string cmd_description(const std::string &cmd) const;

  // List all registered commands
  std::vector<std::string> list_commands() const;

  // List all registered commands with their dangerous status
  std::vector<std::pair<std::string, bool>>
  list_commands_with_dangerous() const;

  // Get help text for all commands (formatted)
  std::string help_text() const;

private:
  std::map<std::string, CmdRoute> routes_;
};

} // namespace astral
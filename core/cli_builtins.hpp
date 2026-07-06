// cli_builtins.hpp — CLI builtin command registration header
#pragma once

namespace astral {

class Shell;
class Agent;

// Namespace for CLI command registration functions
namespace cli {

// Register all AI-independent CLI builtins (help, ls, cd, home, mask, etc.)
// Does NOT register mode-specific commands (/plan, /act, /debug) which
// are handled directly in main.cpp due to local variable dependencies.
void register_all(Shell &shell, Agent &agent);

} // namespace cli
} // namespace astral
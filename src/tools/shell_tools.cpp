#include "shell_tools.h"
#include <cstdio>
#include <array>
#include <memory>

std::string ShellExecTool::name() const { return "shell_exec"; }
std::string ShellExecTool::description() const { return "Execute a shell command"; }

json ShellExecTool::parameters_schema() const {
    return {
        {"command", {{"type", "string"}, {"description", "Shell command to execute"}}},
        {"timeout_ms", {{"type", "integer"}, {"description", "Timeout in milliseconds"}}}
    };
}

ToolResult ShellExecTool::execute(const json& params) {
    std::string cmd = params["command"].get<std::string>();
    std::array<char, 256> buffer;
    std::string output;

#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif

    if (!pipe) return {false, "Failed to execute command"};

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    return {true, output};
}

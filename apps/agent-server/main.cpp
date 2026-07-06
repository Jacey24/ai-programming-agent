#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int run_agent_server(const std::string& config_path);

int main(int argc, char** argv) {
    std::string config_path = "config/agent.json";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    return run_agent_server(config_path);
}

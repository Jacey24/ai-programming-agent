#include "domain/agent/AgentConfiguration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;
using namespace codepilot;

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

} // namespace

int main() {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("codepilot-agent-config-test-" + std::to_string(unique));
  const auto configPath = directory / "experts.json";
  try {
    std::filesystem::create_directories(directory);
    const json original = {
        {"_ui", {{"workspace_positions", {{"workspace-a", {{"planner", {{"x", 12}, {"y", 34}}}}}}}}},
        {"experts", {{{"name", "planner"}, {"description", "before"}, {"is_entry", true}}}}};
    {
      std::ofstream output(configPath, std::ios::trunc);
      output << original.dump(2);
    }

    auto &configuration = AgentConfiguration::getInstance();
    require(configuration.init(configPath.string()), "configuration init failed");
    require(configuration.patchExpert("planner", R"({"description":"after"})"),
            "expert patch failed");
    require(configuration.saveToFile(), "configuration save failed");

    std::ifstream input(configPath);
    const json saved = json::parse(input);
    input.close();
    require(saved["_ui"] == original["_ui"], "UI position metadata was lost");
    require(saved["experts"][0]["description"] == "after",
            "expert patch was not persisted");

    std::filesystem::remove_all(directory);
    std::cout << "Agent configuration save tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}

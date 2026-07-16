#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  namespace fs = std::filesystem;

  const auto unique_suffix =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  const fs::path runtime_root =
      fs::current_path() / "test-runtime" / ("cpp-" + unique_suffix);

  try {
    for (const char *directory : {"config", "logs", "storage", "workspace"}) {
      require(fs::create_directories(runtime_root / directory),
              std::string("failed to create ") + directory);
    }

    const fs::path marker = runtime_root / "workspace" / "round-trip.txt";
    {
      std::ofstream output(marker, std::ios::binary);
      require(output.is_open(), "failed to create workspace marker");
      output << "isolated test runtime";
    }

    {
      std::ifstream input(marker, std::ios::binary);
      std::string contents;
      std::getline(input, contents);
      require(contents == "isolated test runtime", "workspace marker mismatch");
    }

    require(fs::remove_all(runtime_root) > 0, "failed to clean test runtime");
    require(!fs::exists(runtime_root), "test runtime still exists after cleanup");

    std::error_code ignored;
    fs::remove(runtime_root.parent_path(), ignored);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Temporary runtime test failed: " << error.what() << '\n'
              << "Preserved test runtime: " << runtime_root.string() << '\n';
    return 1;
  }
}

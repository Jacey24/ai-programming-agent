#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int run_agent_server(const std::string &config_path, bool enableConsole);

// Resolve the runtime root from the executable, never from the caller's CWD.
static std::filesystem::path findRuntimeRoot() {
  std::filesystem::path exeDir;
#ifdef _WIN32
  std::array<wchar_t, 32768> buffer{};
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    throw std::runtime_error("GetModuleFileNameW failed (Win32 error " +
                             std::to_string(GetLastError()) + ")");
  }
  exeDir = std::filesystem::path(buffer.data()).parent_path();
#else
  // Linux: 读取 /proc/self/exe
  exeDir = std::filesystem::canonical("/proc/self/exe").parent_path();
#endif

  std::error_code error;
  auto current = std::filesystem::absolute(exeDir, error);
  if (error) {
    throw std::runtime_error("cannot resolve executable directory '" +
                             exeDir.string() + "': " + error.message());
  }

  // Portable packages keep config next to the executable. Test and build
  // layouts may keep the executable in a bin/configuration subdirectory.
  for (int i = 0; i < 8; ++i) {
    error.clear();
    if (std::filesystem::is_regular_file(current / "config" / "agent.json",
                                         error)) {
      return current;
    }
    if (error) {
      if (error == std::errc::no_such_file_or_directory ||
          error == std::errc::not_a_directory) {
        error.clear();
      } else {
        throw std::runtime_error("cannot inspect runtime directory '" +
                                 current.string() + "': " + error.message());
      }
    }
    const auto parent = current.parent_path();
    if (parent.empty() || parent == current) {
      break;
    }
    current = parent;
  }

  // A custom --config may still be supplied, but all relative runtime paths
  // remain anchored to the executable directory.
  return std::filesystem::absolute(exeDir);
}

int main(int argc, char **argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  try {
    const auto runtimeRoot = findRuntimeRoot();
    std::filesystem::current_path(runtimeRoot);
    std::cout << "[INFO] Runtime root: " << runtimeRoot.string() << std::endl;

    std::string config_path = "config/agent.json";
    bool enableConsole = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--config") {
        if (i + 1 >= argc) {
          std::cerr << "[FATAL][Configuration] --config requires a path"
                    << std::endl;
          return 2;
        }
        config_path = argv[++i];
      } else if (arg == "--console") {
        enableConsole = true;
      }
    }

    return run_agent_server(config_path, enableConsole);
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][Runtime] Unable to determine or enter the runtime "
                 "directory: "
              << error.what() << std::endl;
    return 1;
  }
}

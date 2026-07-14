#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int run_agent_server(const std::string &config_path);

// 从 exe 路径向上查找项目根目录（包含 CMakeLists.txt 或 config/agent.json）
static std::string findProjectRoot() {
  std::filesystem::path exeDir;
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
    exeDir = std::filesystem::path(buf).parent_path();
  } else {
    // 降级：尝试 CWD
    exeDir = std::filesystem::current_path();
  }
#else
  // Linux: 读取 /proc/self/exe
  exeDir = std::filesystem::canonical("/proc/self/exe").parent_path();
#endif

  // 向上最多搜索 6 级，寻找包含 config/agent.json 的目录
  auto current = std::filesystem::absolute(exeDir);
  for (int i = 0; i < 6; ++i) {
    if (std::filesystem::exists(current / "CMakeLists.txt") ||
        std::filesystem::exists(current / "config" / "agent.json")) {
      return current.string();
    }
    current = current.parent_path();
  }
  // 降级：返回 CWD
  return std::filesystem::current_path().string();
}

int main(int argc, char **argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  // 自动定位项目根目录并切换工作目录
  std::string projectRoot = findProjectRoot();
  std::filesystem::current_path(projectRoot);
  std::cout << "[INFO] Project root: " << projectRoot << std::endl;

  std::string config_path = "config/agent.json";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    }
  }

  return run_agent_server(config_path);
}
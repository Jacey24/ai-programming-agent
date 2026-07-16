#include "infrastructure/process/ProcessRunner.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
int emitAnsiOutput() {
  const std::wstring text = L"目标值 7 的索引为: 3\r\n";
  const int size = WideCharToMultiByte(GetACP(), 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return 1;
  }

  std::string bytes(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(GetACP(), 0, text.data(),
                          static_cast<int>(text.size()), bytes.data(), size,
                          nullptr, nullptr) <= 0) {
    return 1;
  }
  std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return 0;
}
#endif

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  if (argc > 1 && std::string(argv[1]) == "--emit-ansi") {
    return emitAnsiOutput();
  }

  const std::string executable =
      std::filesystem::absolute(argv[0]).string();
  codepilot::ProcessRunner runner;
  const codepilot::ProcessResult result =
      runner.execute("\"" + executable + "\" --emit-ansi", 10);

  const std::string expected = "目标值 7 的索引为: 3\r\n";
  if (!result.success || result.exitCode != 0 || result.output != expected) {
    std::cerr << "ANSI output was not normalized to UTF-8\n";
    std::cerr << "success=" << result.success
              << " exitCode=" << result.exitCode << " output=" << result.output
              << "\n";
    return 1;
  }

  try {
    const nlohmann::json payload{{"output", result.output}};
    const std::string serialized = payload.dump();
    if (serialized.empty()) {
      std::cerr << "JSON serialization returned an empty payload\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "UTF-8 output could not be serialized: " << error.what()
              << "\n";
    return 1;
  }
#else
  (void)argc;
  (void)argv;
#endif

  return 0;
}

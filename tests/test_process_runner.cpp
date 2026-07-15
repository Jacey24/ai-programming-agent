#include "infrastructure/process/ProcessRunner.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

#ifdef _WIN32
int emitGbkOutput() {
  const unsigned char bytes[] = {
      0xC4, 0xBF, 0xB1, 0xEA, 0xD6, 0xB5, 0x20, 0x37, 0x20, 0xB5, 0xC4,
      0xCB, 0xF7, 0xD2, 0xFD, 0xCE, 0xAA, 0x3A, 0x20, 0x33, 0x0D, 0x0A};
  std::cout.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
  return 0;
}
#endif

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  if (argc > 1 && std::string(argv[1]) == "--emit-gbk") {
    return emitGbkOutput();
  }

  const std::string executable =
      std::filesystem::absolute(argv[0]).string();
  codepilot::ProcessRunner runner;
  const codepilot::ProcessResult result =
      runner.execute("\"" + executable + "\" --emit-gbk", 10);

  const std::string expected = "目标值 7 的索引为: 3\r\n";
  if (!result.success || result.exitCode != 0 || result.output != expected) {
    std::cerr << "GBK output was not normalized to UTF-8\n";
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

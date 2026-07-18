#include "ProcessGuard.h"
#include "ShellWindow.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// Try to locate the backend executable. The search order is:
//   1. Same directory as the shell (production install layout)
//   2. ../../agent-server/Release/  (CMake multi-config build tree)
//   3. ../agent-server/Release/     (CMake single-config build tree)
static std::filesystem::path findBackend() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  const std::filesystem::path shellDir =
      std::filesystem::path(buffer).parent_path();

  const std::vector<std::filesystem::path> candidates = {
      shellDir / L"codepilot-agent-server.exe",
      shellDir / L"../../agent-server/Release/codepilot-agent-server.exe",
      shellDir / L"../agent-server/Release/codepilot-agent-server.exe",
  };

  for (const auto &candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return std::filesystem::canonical(candidate, ec);
    }
  }

  return {}; // not found
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

#if !defined(NDEBUG)
  AllocConsole();
  FILE *fDummy;
  freopen_s(&fDummy, "CONOUT$", "w", stdout);
  freopen_s(&fDummy, "CONOUT$", "w", stderr);
  std::wcout << L"=== CodePilot Shell (Diagnostics) ===" << std::endl;
#endif

  const auto backendPath = findBackend();
  if (backendPath.empty()) {
    MessageBoxW(nullptr,
                L"CodePilot Agent Server not found.\n\n"
                L"Please reinstall the application or contact support.",
                L"CodePilot - Error", MB_OK | MB_ICONERROR);
    return 1;
  }
  std::wcout << L"[Shell] Backend found: " << backendPath.wstring()
             << std::endl;

  // Spawn the backend. Its working directory is the one containing the exe,
  // so that it can find config/, web/, etc. relative to itself.
  codepilot::shell::ProcessGuard guard;
  if (!guard.spawnBackend(backendPath.wstring())) {
    MessageBoxW(nullptr,
                L"Failed to start CodePilot Agent Server.\n\n"
                L"Please check your system configuration.",
                L"CodePilot - Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  // Wait for backend health.
  const int backendPort = 8080;
  std::cout << "[Shell] Waiting for backend to become healthy..." << std::endl;
  if (!guard.waitForHealthy(backendPort, 15)) {
    MessageBoxW(nullptr,
                L"CodePilot Agent Server did not start in time.\n\n"
                L"Please check logs and try again.",
                L"CodePilot - Error", MB_OK | MB_ICONERROR);
    guard.shutdown();
    return 1;
  }

  // Create the shell window.
  codepilot::shell::ShellWindow window(hInstance, nCmdShow, backendPort);
  if (!window.create()) {
    std::cerr << "[Shell] Failed to create window." << std::endl;
    MessageBoxW(nullptr,
                L"Failed to create application window.\n\n"
                L"Please ensure WebView2 Runtime is installed.",
                L"CodePilot - Error", MB_OK | MB_ICONERROR);
    guard.shutdown();
    return 1;
  }

  // Run the message loop.
  int result = window.runMessageLoop();

  // Shutdown backend when window closes.
  guard.shutdown();

  return result;
}

#include "ProcessGuard.h"
#include "ShellWindow.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <thread>


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  // Locate the backend executable relative to the shell.
  std::filesystem::path shellPath;
  {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    shellPath = std::filesystem::path(buffer).parent_path();
  }
  std::filesystem::path backendPath = shellPath / L"codepilot-agent-server.exe";

  if (!std::filesystem::exists(backendPath)) {
    std::wcerr << L"[Shell] Backend executable not found: " << backendPath
               << std::endl;
    MessageBoxW(nullptr,
                L"CodePilot Agent Server not found.\n\n"
                L"Please reinstall the application or contact support.",
                L"CodePilot - Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  // Spawn the backend.
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
#include "ProcessGuard.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#include <winhttp.h>

namespace codepilot {
namespace shell {

ProcessGuard::ProcessGuard() = default;

ProcessGuard::~ProcessGuard() {
  shutdown();
  if (stderrRead_)
    CloseHandle(stderrRead_);
}

bool ProcessGuard::spawnBackend(const std::wstring &executablePath) {
  if (spawned_) {
    return false;
  }

  // Build command line: the exe itself, no extra args needed.
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE; // Backend runs hidden, no console window.

  PROCESS_INFORMATION pi{};

  // We need a writable copy for CreateProcess.
  std::wstring cmdLine = L"\"" + executablePath + L"\"";

  // Set the backend's working directory to the folder containing its
  // executable so it can find config/, web/, storage/, etc. via
  // relative paths (e.g. "./web", "./config/agent.json").
  std::filesystem::path backendDir =
      std::filesystem::path(executablePath).parent_path();
  std::wstring workDir = backendDir.wstring();

  if (!CreateProcessW(nullptr,          // lpApplicationName
                      cmdLine.data(),   // lpCommandLine (writable)
                      nullptr,          // lpProcessAttributes
                      nullptr,          // lpThreadAttributes
                      FALSE,            // bInheritHandles
                      CREATE_NO_WINDOW, // dwCreationFlags
                      nullptr,          // lpEnvironment
                      workDir.c_str(),  // lpCurrentDirectory
                      &si, &pi)) {
    std::cerr << "[Shell] Failed to start backend: " << GetLastError()
              << std::endl;
    return false;
  }

  CloseHandle(pi.hThread);
  processInfo_ = pi;
  spawned_ = true;
  std::cout << "[Shell] Backend process started (PID " << pi.dwProcessId << ")"
            << std::endl;
  return true;
}

bool ProcessGuard::waitForHealthy(int port, int timeoutSeconds) {
  if (!spawned_) {
    return false;
  }

  std::wstring host = L"localhost";
  DWORD portDword = static_cast<DWORD>(port);

  auto startTime = std::chrono::steady_clock::now();

  while (true) {
    // Check if process is still alive.
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInfo_.hProcess, &exitCode) &&
        exitCode != STILL_ACTIVE) {
      std::cerr << "[Shell] Backend process exited prematurely with code "
                << exitCode << std::endl;
      spawned_ = false;
      return false;
    }

    // Try a simple HTTP GET /api/v1/health.
    HINTERNET hSession =
        WinHttpOpen(L"CodePilot-Shell/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), portDword, 0);
    if (!hConnect) {
      WinHttpCloseHandle(hSession);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    HINTERNET hRequest =
        WinHttpOpenRequest(hConnect, L"GET", L"/api/v1/health", nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (result) {
      result = WinHttpReceiveResponse(hRequest, nullptr);
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (result) {
      WinHttpQueryHeaders(hRequest,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                          &statusSize, WINHTTP_NO_HEADER_INDEX);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode == 200) {
      std::cout << "[Shell] Backend is healthy on port " << port << std::endl;
      return true;
    }

    // Check timeout.
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - startTime)
            .count();
    if (elapsed >= timeoutSeconds) {
      std::cerr << "[Shell] Backend health check timed out after "
                << timeoutSeconds << " seconds" << std::endl;
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

void ProcessGuard::shutdown() {
  if (!spawned_) {
    return;
  }

  std::cout << "[Shell] Shutting down backend (PID " << processInfo_.dwProcessId
            << ")..." << std::endl;

  // Try graceful termination.
  DWORD exitCode = 0;
  if (GetExitCodeProcess(processInfo_.hProcess, &exitCode) &&
      exitCode == STILL_ACTIVE) {
    // Send Ctrl+C via GenerateConsoleCtrlEvent to a process group.
    // Since the backend creates its own console, try TerminateProcess as
    // the last resort.
    TerminateProcess(processInfo_.hProcess, 0);
  }

  WaitForSingleObject(processInfo_.hProcess, 5000);
  CloseHandle(processInfo_.hProcess);
  processInfo_ = {};
  spawned_ = false;
  std::cout << "[Shell] Backend shut down." << std::endl;
}

std::string ProcessGuard::readStderrTail(std::size_t maxLines) const {
  // Read the most recent log file from the backend's logs/ directory.
  // This is a best-effort diagnostic: if the backend process dies before
  // logging starts, the result will be empty.
  if (!spawned_)
    return {};

  // Find the backend executable directory from processInfo_ is not directly
  // available, so we search relative to the shell executable.
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::filesystem::path shellDir = std::filesystem::path(buffer).parent_path();
  std::filesystem::path logsDir;

  const std::vector<std::filesystem::path> candidates = {
      shellDir / L"logs",
      shellDir / L".." / L".." / L"logs",
  };
  for (const auto &c : candidates) {
    std::error_code ec;
    if (std::filesystem::is_directory(c, ec) && !ec) {
      logsDir = c;
      break;
    }
  }
  if (logsDir.empty())
    return {};

  // Find the newest .log file
  std::filesystem::path newestLog;
  std::filesystem::file_time_type newestTime;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(logsDir, ec)) {
    if (!entry.is_regular_file(ec) || ec)
      continue;
    if (entry.path().extension() != L".log")
      continue;
    const auto wt = entry.last_write_time(ec);
    if (ec)
      continue;
    if (newestLog.empty() || wt > newestTime) {
      newestLog = entry.path();
      newestTime = wt;
    }
  }
  if (newestLog.empty())
    return {};

  // Read the full file, take last N lines
  std::ifstream in(newestLog, std::ios::ate);
  if (!in)
    return {};
  const auto size = in.tellg();
  if (size <= 0)
    return {};
  // Read at most the last 16 KB
  const auto readSize = std::min<std::streamsize>(size, 16 * 1024);
  in.seekg(-readSize, std::ios::end);
  std::string content(readSize, '\0');
  in.read(content.data(), readSize);
  content.resize(in.gcount());

  // Extract last maxLines
  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line))
    lines.push_back(line);
  if (lines.size() <= maxLines) {
    std::ostringstream oss;
    for (const auto &l : lines)
      oss << l << "\n";
    return oss.str();
  }
  std::ostringstream oss;
  for (std::size_t i = lines.size() - maxLines; i < lines.size(); ++i)
    oss << lines[i] << "\n";
  return oss.str();
}

bool ProcessGuard::isRunning() const {
  if (!spawned_) {
    return false;
  }
  DWORD exitCode = 0;
  if (GetExitCodeProcess(processInfo_.hProcess, &exitCode)) {
    return exitCode == STILL_ACTIVE;
  }
  return false;
}

} // namespace shell
} // namespace codepilot
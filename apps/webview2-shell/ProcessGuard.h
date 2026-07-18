#pragma once

#include <string>
#include <windows.h>

namespace codepilot {
namespace shell {

class ProcessGuard {
public:
  ProcessGuard();
  ~ProcessGuard();

  ProcessGuard(const ProcessGuard &) = delete;
  ProcessGuard &operator=(const ProcessGuard &) = delete;

  // Spawn the backend agent server as a child process.
  // executablePath: path to codepilot-agent-server.exe
  // Returns true on success, false on failure.
  bool spawnBackend(const std::wstring &executablePath);

  // Wait for backend to become healthy by polling /api/v1/health.
  // timeoutSeconds: maximum time to wait before giving up.
  // port: the port the backend is expected to listen on.
  // Returns true when healthy, false on timeout.
  bool waitForHealthy(int port, int timeoutSeconds);

  // Request graceful shutdown via taskkill, then fall back to TerminateProcess.
  void shutdown();

  // Check if the backend process is still running.
  bool isRunning() const;

  // Read captured stderr from the backend process (for diagnostics).
  std::string readStderrTail(std::size_t maxLines = 20) const;

private:
  PROCESS_INFORMATION processInfo_{};
  bool spawned_{false};
  HANDLE stderrRead_{nullptr};
  HANDLE stderrWrite_{nullptr};
};

} // namespace shell
} // namespace codepilot
#pragma once

#include <memory>
#include <string>
#include <windows.h>

namespace codepilot {
namespace shell {

// Forward declarations — WebView2 types are only needed in the .cpp file.
// This avoids the include-order-sensitive <WebView2.h> in the header.
struct Impl;
class ShellWindow {
public:
  ShellWindow(HINSTANCE hInstance, int nCmdShow, int port);
  ~ShellWindow();

  ShellWindow(const ShellWindow &) = delete;
  ShellWindow &operator=(const ShellWindow &) = delete;

  bool create();
  int runMessageLoop();

  // Backend port (needed by Impl).
  int port() const;

private:
  friend struct Impl;
  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);
  LRESULT handleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  bool handleCreate(HWND hWnd);
  void handleDestroy();
  void handleResize();

  HINSTANCE hInstance_;
  int nCmdShow_;
  int port_;
  HWND hWnd_{nullptr};

  // Opaque pointer to WebView2 implementation details.
  std::unique_ptr<Impl> impl_;

  static constexpr const wchar_t *ClassName = L"CodePilotShellWindow";
};

} // namespace shell
} // namespace codepilot
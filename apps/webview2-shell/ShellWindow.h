#pragma once

#include <memory>
#include <string>
#include <windows.h>

namespace codepilot {
namespace shell {

struct Impl;
class ShellWindow {
public:
  ShellWindow(HINSTANCE hInstance, int nCmdShow, int port);
  ~ShellWindow();

  ShellWindow(const ShellWindow &) = delete;
  ShellWindow &operator=(const ShellWindow &) = delete;

  bool create();
  int runMessageLoop();

  int port() const;
  HWND mainHwnd() const;

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

  std::unique_ptr<Impl> impl_;

  static constexpr const wchar_t *ClassName = L"CodePilotShellWindow";
  static constexpr int TITLE_BAR_HEIGHT = 42;
};

} // namespace shell
} // namespace codepilot
#include "ShellWindow.h"

// order-sensitive: windows.h first, then wrl before wil/com.h for Callback
// clang-format off
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <wil/com.h>
#include <dwmapi.h>
// clang-format on

#include <iostream>
#include <memory>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")

namespace codepilot {
namespace shell {

// Pimpl — all WebView2 types are confined to this translation unit.
struct Impl {
  wil::com_ptr<ICoreWebView2Environment> environment;
  wil::com_ptr<ICoreWebView2Controller> controller;
  wil::com_ptr<ICoreWebView2> webview;

  bool initWebView2(HWND hWnd, ShellWindow &window);
  void navigateToApp(int port);
};

bool Impl::initWebView2(HWND hWnd, ShellWindow &window) {
  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, nullptr, nullptr,
      Microsoft::WRL::Callback<
          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this, hWnd, &window](HRESULT result,
                                ICoreWebView2Environment *env) -> HRESULT {
            if (FAILED(result)) {
              std::cerr << "[Shell] WebView2 environment creation failed: 0x"
                        << std::hex << result << std::endl;
              return result;
            }
            environment = env;

            env->CreateCoreWebView2Controller(
                hWnd,
                Microsoft::WRL::Callback<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, &window](HRESULT result,
                                    ICoreWebView2Controller *ctrl) -> HRESULT {
                      if (FAILED(result)) {
                        std::cerr
                            << "[Shell] WebView2 controller creation failed: 0x"
                            << std::hex << result << std::endl;
                        return result;
                      }
                      controller = ctrl;
                      controller->get_CoreWebView2(&webview);

                      if (webview) {
                        wil::com_ptr<ICoreWebView2Settings> settings;
                        webview->get_Settings(&settings);
                        if (settings) {
                          settings->put_IsScriptEnabled(TRUE);
                          settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                          settings->put_IsWebMessageEnabled(FALSE);
                          settings->put_AreDevToolsEnabled(FALSE);
                        }

                        wil::com_ptr<ICoreWebView2Controller2> ctrl2 =
                            controller.try_query<ICoreWebView2Controller2>();
                        if (ctrl2) {
                          COREWEBVIEW2_COLOR bg{};
                          bg.R = 18;
                          bg.G = 18;
                          bg.B = 22;
                          bg.A = 255;
                          ctrl2->put_DefaultBackgroundColor(bg);
                        }
                      }

                      window.handleResize();
                      navigateToApp(window.port());
                      return S_OK;
                    })
                    .Get());

            return S_OK;
          })
          .Get());

  if (FAILED(hr)) {
    std::cerr << "[Shell] CreateCoreWebView2EnvironmentWithOptions failed: 0x"
              << std::hex << hr << std::endl;
    return false;
  }
  return true;
}

void Impl::navigateToApp(int port) {
  if (!webview)
    return;
  std::wstring url = L"http://localhost:" + std::to_wstring(port) + L"/";
  std::wcout << L"[Shell] Navigating to " << url << std::endl;
  webview->Navigate(url.c_str());
}

// -------------------------------------------------------------------------
// ShellWindow
// -------------------------------------------------------------------------

ShellWindow::ShellWindow(HINSTANCE hInstance, int nCmdShow, int port)
    : hInstance_(hInstance), nCmdShow_(nCmdShow), port_(port),
      impl_(std::make_unique<Impl>()) {}

ShellWindow::~ShellWindow() {
  if (impl_ && impl_->controller) {
    impl_->controller->Close();
  }
}

int ShellWindow::port() const { return port_; }

bool ShellWindow::create() {
  WNDCLASSEXW wcex{};
  wcex.cbSize = sizeof(WNDCLASSEXW);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.hInstance = hInstance_;
  wcex.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wcex.hbrBackground = CreateSolidBrush(RGB(18, 18, 22));
  wcex.lpszClassName = ClassName;

  if (!RegisterClassExW(&wcex)) {
    std::cerr << "[Shell] RegisterClassEx failed: " << GetLastError()
              << std::endl;
    return false;
  }

  int width = 1200, height = 800;
  HMONITOR hMonitor = MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
  if (hMonitor) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
      RECT work = mi.rcWork;
      width = static_cast<int>((work.right - work.left) * 0.85);
      height = static_cast<int>((work.bottom - work.top) * 0.85);
    }
  }

  int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

  hWnd_ = CreateWindowExW(0, ClassName, L"CodePilot", WS_POPUP, x, y, width,
                          height, nullptr, nullptr, hInstance_, this);
  if (!hWnd_) {
    std::cerr << "[Shell] CreateWindowEx failed: " << GetLastError()
              << std::endl;
    return false;
  }

  BOOL useDarkMode = TRUE;
  DwmSetWindowAttribute(hWnd_, 20, &useDarkMode, sizeof(useDarkMode));
  MARGINS margins{0, 0, 0, 1};
  DwmExtendFrameIntoClientArea(hWnd_, &margins);

  ShowWindow(hWnd_, nCmdShow_);
  UpdateWindow(hWnd_);
  return true;
}

int ShellWindow::runMessageLoop() {
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK ShellWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
  ShellWindow *self = nullptr;
  if (msg == WM_NCCREATE) {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<ShellWindow *>(cs->lpCreateParams);
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hWnd_ = hWnd;
  } else {
    self =
        reinterpret_cast<ShellWindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
  }
  if (self)
    return self->handleMessage(hWnd, msg, wParam, lParam);
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT ShellWindow::handleMessage(HWND hWnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    if (!handleCreate(hWnd))
      return -1;
    return 0;
  case WM_SIZE:
    handleResize();
    return 0;
  case WM_NCHITTEST: {
    LRESULT result = DefWindowProcW(hWnd, msg, wParam, lParam);
    if (result == HTCLIENT) {
      POINT pt{LOWORD(lParam), HIWORD(lParam)};
      ScreenToClient(hWnd, &pt);
      RECT rc;
      GetClientRect(hWnd, &rc);
      if (pt.y < 40 && pt.x < rc.right - 120)
        return HTCAPTION;
    }
    return result;
  }
  case WM_DESTROY:
    handleDestroy();
    PostQuitMessage(0);
    return 0;
  case WM_CLOSE:
    DestroyWindow(hWnd);
    return 0;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool ShellWindow::handleCreate(HWND hWnd) {
  return impl_->initWebView2(hWnd, *this);
}

void ShellWindow::handleDestroy() {
  if (impl_ && impl_->controller) {
    impl_->controller->Close();
    impl_->controller = nullptr;
  }
  impl_->webview = nullptr;
  impl_->environment = nullptr;
}

void ShellWindow::handleResize() {
  if (impl_ && impl_->controller) {
    RECT bounds;
    GetClientRect(hWnd_, &bounds);
    impl_->controller->put_Bounds(bounds);
  }
}

} // namespace shell
} // namespace codepilot
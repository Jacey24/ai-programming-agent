#include "ShellWindow.h"

// order-sensitive: windows.h first, then wrl before wil/com.h for Callback
// clang-format off
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wil/com.h>
#include <dwmapi.h>
#include <shlobj.h>
// clang-format on

#include <iostream>
#include <memory>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

namespace codepilot {
namespace shell {

// =========================================================================
// Pimpl
// =========================================================================
struct Impl {
  wil::com_ptr<ICoreWebView2Environment> environment;
  wil::com_ptr<ICoreWebView2Controller> controller;
  wil::com_ptr<ICoreWebView2> webview;
  EventRegistrationToken navCompletedToken_{};
  EventRegistrationToken webMsgToken_{};

  bool initWebView2(HWND hWnd, ShellWindow &window);
  void navigateToApp(int port);
  void postMessageToFrontend(const std::wstring &type);
  void setTitleBarTheme(HWND hWnd, bool dark);
};

bool Impl::initWebView2(HWND hWnd, ShellWindow &window) {
  auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  options->put_AdditionalBrowserArguments(L"--allow-insecure-localhost");

  std::wcout << L"[Shell] Creating WebView2 environment..." << std::endl;

  // Use LOCALAPPDATA to avoid Program Files write permission issues
  wchar_t localAppData[MAX_PATH];
  std::wstring userDataFolder;
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                 localAppData))) {
    userDataFolder = std::wstring(localAppData) + L"\\CodePilot\\WebView2";
    // Ensure the directory tree exists
    std::wstring codePilotDir = std::wstring(localAppData) + L"\\CodePilot";
    CreateDirectoryW(codePilotDir.c_str(), nullptr);
    CreateDirectoryW(userDataFolder.c_str(), nullptr);
  } else {
    userDataFolder = L"./webview2_data";
    CreateDirectoryW(userDataFolder.c_str(), nullptr);
  }

  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userDataFolder.c_str(), options.Get(),
      Microsoft::WRL::Callback<
          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this, hWnd, &window](HRESULT result,
                                ICoreWebView2Environment *env) -> HRESULT {
            if (FAILED(result)) {
              std::wcerr << L"[Shell] WebView2 env creation failed: 0x"
                         << std::hex << result << std::endl;
              return result;
            }
            std::wcout << L"[Shell] Env created OK, creating controller..."
                       << std::endl;
            environment = env;

            env->CreateCoreWebView2Controller(
                hWnd,
                Microsoft::WRL::Callback<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, &window](HRESULT result,
                                    ICoreWebView2Controller *ctrl) -> HRESULT {
                      if (FAILED(result)) {
                        std::wcerr << L"[Shell] Controller creation failed: 0x"
                                   << std::hex << result << std::endl;
                        return result;
                      }
                      std::wcout << L"[Shell] Controller created OK"
                                 << std::endl;
                      controller = ctrl;
                      controller->get_CoreWebView2(&webview);

                      if (webview) {
                        std::wcout
                            << L"[Shell] Configuring WebView2 settings..."
                            << std::endl;
                        wil::com_ptr<ICoreWebView2Settings> settings;
                        webview->get_Settings(&settings);
                        if (settings) {
                          settings->put_IsScriptEnabled(TRUE);
                          settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                          settings->put_IsWebMessageEnabled(TRUE);
                          settings->put_AreDevToolsEnabled(TRUE);
                        }

                        // NavigationCompleted diagnostics
                        webview->add_NavigationCompleted(
                            Microsoft::WRL::Callback<
                                ICoreWebView2NavigationCompletedEventHandler>(
                                [](ICoreWebView2 *sender,
                                   ICoreWebView2NavigationCompletedEventArgs
                                       *args) -> HRESULT {
                                  BOOL success = FALSE;
                                  COREWEBVIEW2_WEB_ERROR_STATUS error{};
                                  args->get_IsSuccess(&success);
                                  args->get_WebErrorStatus(&error);
                                  std::wcout << L"[Shell] NavigationCompleted "
                                             << L"isSuccess=" << success
                                             << L" webError=0x" << std::hex
                                             << (int)error << std::dec
                                             << std::endl;
                                  return S_OK;
                                })
                                .Get(),
                            &navCompletedToken_);

                        // WebMessageReceived
                        webview->add_WebMessageReceived(
                            Microsoft::WRL::Callback<
                                ICoreWebView2WebMessageReceivedEventHandler>(
                                [&window](
                                    ICoreWebView2 *sender,
                                    ICoreWebView2WebMessageReceivedEventArgs
                                        *args) -> HRESULT {
                                  wil::unique_cotaskmem_string rawJson;
                                  args->TryGetWebMessageAsString(&rawJson);
                                  if (rawJson) {
                                    std::wstring msg(rawJson.get());
                                    std::wcout
                                        << L"[Shell] WebMessage received: "
                                        << msg << std::endl;

                                    Impl *impl = window.impl_.get();
                                    if (msg.find(L"\"set-theme\"") !=
                                        std::wstring::npos) {
                                      bool dark = msg.find(L"\"dark\"") !=
                                                  std::wstring::npos;
                                      impl->setTitleBarTheme(window.mainHwnd(),
                                                             dark);
                                    }
                                  }
                                  return S_OK;
                                })
                                .Get(),
                            &webMsgToken_);

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
                      } else {
                        std::wcerr << L"[Shell] ERROR: webview is NULL after "
                                      L"get_CoreWebView2!"
                                   << std::endl;
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
    std::wcerr << L"[Shell] CreateCoreWebView2EnvironmentWithOptions failed: 0x"
               << std::hex << hr << std::endl;
    return false;
  }
  return true;
}

void Impl::navigateToApp(int port) {
  if (!webview) {
    std::wcerr << L"[Shell] Cannot navigate: webview is NULL" << std::endl;
    return;
  }
  std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
  std::wcout << L"[Shell] Navigating to " << url << std::endl;
  webview->Navigate(url.c_str());
}

void Impl::postMessageToFrontend(const std::wstring &type) {
  if (!webview)
    return;
  std::wstring json = L"{\"type\":\"" + type + L"\"}";
  webview->PostWebMessageAsJson(json.c_str());
  std::wcout << L"[Shell] PostWebMessageAsJson: " << json << std::endl;
}

void Impl::setTitleBarTheme(HWND hWnd, bool dark) {
  BOOL useDark = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hWnd, 20, &useDark, sizeof(useDark));
  std::wcout << L"[Shell] Title bar theme set to "
             << (dark ? L"dark" : L"light") << std::endl;
}

// =========================================================================
// ShellWindow
// =========================================================================

ShellWindow::ShellWindow(HINSTANCE hInstance, int nCmdShow, int port)
    : hInstance_(hInstance), nCmdShow_(nCmdShow), port_(port),
      impl_(std::make_unique<Impl>()) {}

ShellWindow::~ShellWindow() {
  if (impl_ && impl_->controller) {
    impl_->controller->Close();
  }
}

int ShellWindow::port() const { return port_; }
HWND ShellWindow::mainHwnd() const { return hWnd_; }

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

  // Use standard window with title bar, resizable border, min/max/close
  hWnd_ = CreateWindowExW(0, ClassName, L"CodePilot", WS_OVERLAPPEDWINDOW, x, y,
                          width, height, nullptr, nullptr, hInstance_, this);
  if (!hWnd_) {
    std::cerr << "[Shell] CreateWindowEx failed: " << GetLastError()
              << std::endl;
    return false;
  }

  // Dark mode title bar
  BOOL useDarkMode = TRUE;
  DwmSetWindowAttribute(hWnd_, 20, &useDarkMode, sizeof(useDarkMode));

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
    // Allow dragging on the front-end header (the band just below the system
    // title bar where StatusPills + buttons are).
    LRESULT result = DefWindowProcW(hWnd, msg, wParam, lParam);
    if (result == HTCLIENT) {
      POINT pt{LOWORD(lParam), HIWORD(lParam)};
      ScreenToClient(hWnd, &pt);
      RECT rc;
      GetClientRect(hWnd, &rc);
      // Header band (top 44px) excluding rightmost 120px → draggable
      if (pt.y < 44 && pt.x < rc.right - 120)
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
  std::wcout << L"[Shell] handleCreate — calling initWebView2..." << std::endl;
  return impl_->initWebView2(hWnd, *this);
}

void ShellWindow::handleDestroy() {
  std::wcout << L"[Shell] handleDestroy" << std::endl;
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
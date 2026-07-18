cmake_minimum_required(VERSION 3.21)

include(FetchContent)

# ============================================================
# WebView2 SDK — download directly from NuGet.org (no nuget CLI needed)
# ============================================================
set(WEBVIEW2_VERSION "1.0.2903.40")
set(WEBVIEW2_PACKAGES_DIR "${CMAKE_BINARY_DIR}/_deps/webview2-sdk")
set(WEBVIEW2_SDK_ROOT "${WEBVIEW2_PACKAGES_DIR}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}/build/native")

if(NOT EXISTS "${WEBVIEW2_SDK_ROOT}/include/WebView2.h")
  message(STATUS "Downloading Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}...")

  set(WEBVIEW2_NUPKG_URL
    "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
  )
  set(WEBVIEW2_NUPKG "${WEBVIEW2_PACKAGES_DIR}/webview2.nupkg")
  set(WEBVIEW2_EXTRACT_DIR "${WEBVIEW2_PACKAGES_DIR}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")

  file(MAKE_DIRECTORY "${WEBVIEW2_PACKAGES_DIR}")

  file(DOWNLOAD "${WEBVIEW2_NUPKG_URL}" "${WEBVIEW2_NUPKG}"
    STATUS DOWNLOAD_STATUS
    SHOW_PROGRESS
    TLS_VERIFY OFF
    TIMEOUT 120
  )

  list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
  if(NOT STATUS_CODE EQUAL 0)
    list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
    message(FATAL_ERROR "Failed to download WebView2 SDK: ${ERROR_MSG}")
  endif()

  file(MAKE_DIRECTORY "${WEBVIEW2_EXTRACT_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_NUPKG}" DESTINATION "${WEBVIEW2_EXTRACT_DIR}")
  message(STATUS "WebView2 SDK extracted to ${WEBVIEW2_EXTRACT_DIR}")
endif()

# Verify the package.
if(NOT EXISTS "${WEBVIEW2_SDK_ROOT}/include/WebView2.h")
  message(FATAL_ERROR
    "WebView2 SDK not found at ${WEBVIEW2_SDK_ROOT}/include/WebView2.h"
  )
endif()

# WebView2 static loader (no DLL dependency needed at runtime)
add_library(WebView2Loader STATIC IMPORTED)
set_target_properties(WebView2Loader PROPERTIES
  IMPORTED_LOCATION "${WEBVIEW2_SDK_ROOT}/x64/WebView2LoaderStatic.lib"
)
target_include_directories(WebView2Loader INTERFACE
  "${WEBVIEW2_SDK_ROOT}/include"
)

# ============================================================
# WIL (Windows Implementation Libraries) — download from NuGet
# ============================================================
set(WIL_VERSION "1.0.240803.1")
set(WIL_PACKAGES_DIR "${CMAKE_BINARY_DIR}/_deps/wil")
set(WIL_INCLUDE_DIR "${WIL_PACKAGES_DIR}/Microsoft.Windows.ImplementationLibrary.${WIL_VERSION}/include")

if(NOT EXISTS "${WIL_INCLUDE_DIR}/wil/resource.h")
  message(STATUS "Downloading Microsoft.Windows.ImplementationLibrary ${WIL_VERSION}...")

  set(WIL_NUPKG_URL
    "https://www.nuget.org/api/v2/package/Microsoft.Windows.ImplementationLibrary/${WIL_VERSION}"
  )
  set(WIL_NUPKG "${WIL_PACKAGES_DIR}/wil.nupkg")
  set(WIL_EXTRACT_DIR "${WIL_PACKAGES_DIR}/Microsoft.Windows.ImplementationLibrary.${WIL_VERSION}")

  file(MAKE_DIRECTORY "${WIL_PACKAGES_DIR}")

  file(DOWNLOAD "${WIL_NUPKG_URL}" "${WIL_NUPKG}"
    STATUS DOWNLOAD_STATUS_WIL
    SHOW_PROGRESS
    TLS_VERIFY OFF
    TIMEOUT 120
  )

  list(GET DOWNLOAD_STATUS_WIL 0 STATUS_CODE_WIL)
  if(NOT STATUS_CODE_WIL EQUAL 0)
    list(GET DOWNLOAD_STATUS_WIL 1 ERROR_MSG_WIL)
    message(FATAL_ERROR "Failed to download WIL: ${ERROR_MSG_WIL}")
  endif()

  file(MAKE_DIRECTORY "${WIL_EXTRACT_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${WIL_NUPKG}" DESTINATION "${WIL_EXTRACT_DIR}")
  message(STATUS "WIL extracted to ${WIL_EXTRACT_DIR}")
endif()

add_library(wil INTERFACE)
target_include_directories(wil INTERFACE "${WIL_INCLUDE_DIR}")

# ============================================================
# codepilot-shell executable
# ============================================================
add_executable(codepilot-shell WIN32
  main.cpp
  ProcessGuard.cpp
  ShellWindow.cpp
)

target_include_directories(codepilot-shell PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(codepilot-shell PRIVATE
  WebView2Loader
  wil
  Dwmapi          # DwmExtendFrameIntoClientArea, DwmSetWindowAttribute
  winhttp         # For WinHttp (waitForHealthy)
  ws2_32
)

# Use modern Windows APIs
target_compile_definitions(codepilot-shell PRIVATE
  UNICODE
  _UNICODE
)
# NOTE: Do NOT define WIN32_LEAN_AND_MEAN — WebView2.h requires the full
#       COM interface macros from <windows.h> (objbase.h / Unknwn.h).

# GUI app with alloc-console for diagnostics
set_target_properties(codepilot-shell PROPERTIES
  WIN32_EXECUTABLE TRUE
)

# ============================================================
# Install
# ============================================================
install(TARGETS codepilot-shell
  RUNTIME DESTINATION .
  COMPONENT shell
)

# Deploy runtime DLLs
if(WIN32)
  add_custom_command(TARGET codepilot-shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E
      $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:codepilot-shell>>,copy_if_different,true>
      $<TARGET_RUNTIME_DLLS:codepilot-shell>
      $<$<BOOL:$<TARGET_RUNTIME_DLLS:codepilot-shell>>:$<TARGET_FILE_DIR:codepilot-shell>>
    COMMAND_EXPAND_LISTS
    COMMENT "Deploying codepilot-shell runtime DLLs"
  )

  install(DIRECTORY $<TARGET_FILE_DIR:codepilot-shell>/
    DESTINATION .
    COMPONENT shell
    FILES_MATCHING PATTERN "*.dll"
  )
endif()
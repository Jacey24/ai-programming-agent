cmake_minimum_required(VERSION 3.20)

project(CodePilot VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 强制 UTF-8 源字符集和执行字符集，否则中文注释/字符串导致编译错误
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(CODEPILOT_BUILD_TUI "Build the TUI client" ON)
option(CODEPILOT_BUILD_TESTS "Build tests" ON)

if(CODEPILOT_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(apps/agent-server)
add_subdirectory(apps/cli-client)

if(CODEPILOT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CODEPILOT_BUILD_TUI)
    add_subdirectory(apps/tui-client)
endif()

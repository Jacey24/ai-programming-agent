cmake_minimum_required(VERSION 3.20)

project(CodePilot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CODEPILOT_BUILD_TUI "Build the TUI client" ON)
option(CODEPILOT_BUILD_TESTS "Build tests" ON)

add_subdirectory(apps/agent-server)

if(CODEPILOT_BUILD_TUI)
    add_subdirectory(apps/tui-client)
endif()

if(CODEPILOT_BUILD_TESTS)
    enable_testing()
endif()

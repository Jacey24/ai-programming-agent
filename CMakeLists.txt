cmake_minimum_required(VERSION 3.20)
project(ai-programming-agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ftxui REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(CLI11 REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

add_executable(ai-agent src/main.cpp)

target_link_libraries(ai-agent
    ftxui::ftxui
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    CURL::libcurl
    CLI11::CLI11
    spdlog::spdlog
    fmt::fmt
)

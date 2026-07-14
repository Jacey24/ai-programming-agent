add_executable(codepilot-cli
    main.cpp
    ApiClient.cpp
    CliController.cpp
    EventPrinter.cpp
)

target_include_directories(codepilot-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)

# Windows: httplib::Client needs ws2_32
if(WIN32)
    target_link_libraries(codepilot-cli ws2_32 crypt32)
endif()

install(TARGETS codepilot-cli RUNTIME DESTINATION bin)
add_executable(codepilot-agent-server
    main.cpp
    AppBootstrap.cpp
)

target_include_directories(codepilot-agent-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/backend/agent-server
)

target_link_libraries(codepilot-agent-server PRIVATE sqlite3)

install(TARGETS codepilot-agent-server RUNTIME DESTINATION bin)

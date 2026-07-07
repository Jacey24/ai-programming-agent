add_executable(codepilot-agent-server
    main.cpp
    AppBootstrap.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/SessionController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/TaskController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/WorkspaceController.cpp
)

target_include_directories(codepilot-agent-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)

target_link_libraries(codepilot-agent-server PRIVATE sqlite3)

install(TARGETS codepilot-agent-server RUNTIME DESTINATION bin)

# ============================================================
# 工具测试可执行文件（独立于 agent-server）
# ============================================================
add_executable(tool-tests
    ${CMAKE_SOURCE_DIR}/apps/tool-tests/tool_test_main.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/FileTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/BuiltinShell.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/Workspace.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventBus.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventDispatcher.cpp
)

target_include_directories(tool-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)

target_link_libraries(tool-tests PRIVATE
    ${CMAKE_DL_LIBS}
)
# SQLite3 单体源文件（跨平台，无需预装库）
add_library(sqlite3 STATIC
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c
    PROPERTIES LANGUAGE C
)
target_include_directories(sqlite3 PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3
)
if(MSVC)
    target_compile_definitions(sqlite3 PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

add_executable(codepilot-agent-server
    main.cpp
    AppBootstrap.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/HttpServer.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/LogController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/FileChangeController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/ReplayController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/PermissionController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/SessionController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/TaskController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/ToolController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/WorkspaceController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/WorkspaceFileController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/ExpertController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/ConfigController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/GlobalController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/DebugController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/LogService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/FileChangeService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/ReplayService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/SessionService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/TaskService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/ToolSystem.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/WorkspaceService.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/facade/DataAccessFacade.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/facade/LlmClientFacade.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/facade/SSEGateway.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/MessageBus.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/PromptBuilder.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/RouteEngine.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/PlanManager.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/ExpertRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/AgentConfiguration.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/AgentLoop.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/AgentOrchestrator.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/LlmClient.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/OpenAICompatibleClient.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/BuiltinShell.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/FileTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ShellTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/GitTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/RiskDetector.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/debug/Debugger.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/PermissionManager.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/process/ProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventBus.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/Workspace.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/WorkspaceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/WorkspaceManager.cpp
    ${CMAKE_SOURCE_DIR}/modules/common/logging/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/modules/common/logging/Logger.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/GlobalRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/EventRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/FileChangeRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/LogRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/PermissionRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/ReplayRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/SessionRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/TaskRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/ToolCallRepository.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/migrations/MigrationRunner.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/repositories/WorkspaceRepository.cpp
)

target_include_directories(codepilot-agent-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
    ${CMAKE_SOURCE_DIR}/modules
)

target_link_libraries(codepilot-agent-server PRIVATE sqlite3)

# OpenSSL 强制要求 - LLM HTTPS 客户端必需
# 当未通过 -DCMAKE_TOOLCHAIN_FILE 指定 vcpkg 时，手动指定路径
if(NOT DEFINED OPENSSL_ROOT_DIR AND EXISTS "D:/DevTools/vcpkg/installed/x64-windows")
    set(OPENSSL_ROOT_DIR "D:/DevTools/vcpkg/installed/x64-windows")
endif()
find_package(OpenSSL REQUIRED)
target_link_libraries(codepilot-agent-server PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_compile_definitions(codepilot-agent-server PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
message(STATUS "OpenSSL found: ${OPENSSL_VERSION} - LLM HTTPS Client enabled")

if(WIN32)
    target_link_libraries(codepilot-agent-server PRIVATE ws2_32)
endif()

install(TARGETS codepilot-agent-server RUNTIME DESTINATION bin)

# ============================================================
# 工具测试可执行文件（独立于 agent-server）
# Sprint 1: FileTool / BuiltinShell / Workspace / EventBus / EventDispatcher
# Sprint 2: ShellTool / GitTool / RiskDetector / PermissionManager / ProcessRunner
# Sprint 2.5: ToolSystem (facade) / ToolRegistry (groups)
# ============================================================
add_executable(tool-tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/apps/tool-tests/tool_test_main.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/ToolSystem.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/FileTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/BuiltinShell.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ShellTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/GitTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/RiskDetector.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/debug/Debugger.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/PermissionManager.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/Workspace.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/process/ProcessRunner.cpp
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

if(CODEPILOT_BUILD_TESTS)
add_executable(agent-loop-test
    ${CMAKE_SOURCE_DIR}/tests/test_agent_loop.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/application/ToolSystem.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/Agent.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/ContextBuilder.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/Planner.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/PromptAdapter.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/ResponseParser.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/RoleRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/TaskQueue.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/TaskState.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/FileTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/BuiltinShell.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/ShellTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/tools/GitTool.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/RiskDetector.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/debug/Debugger.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/security/PermissionManager.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/filesystem/Workspace.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/process/ProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/LlmClient.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/OpenAICompatibleClient.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventBus.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/event/EventDispatcher.cpp
)

target_include_directories(agent-loop-test PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)

target_link_libraries(agent-loop-test PRIVATE
    ${CMAKE_DL_LIBS}
)

add_test(NAME agent-loop-test COMMAND agent-loop-test)
endif()

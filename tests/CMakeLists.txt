add_executable(process-runner-test
    unit/test_process_runner.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/process/ProcessRunner.cpp
)

target_include_directories(process-runner-test PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)

add_test(NAME process-runner-test COMMAND process-runner-test)
set_tests_properties(process-runner-test PROPERTIES LABELS "unit")

add_executable(temp-runtime-test
    unit/test_temp_runtime.cpp
)

add_test(NAME temp-runtime-test COMMAND temp-runtime-test)
set_tests_properties(temp-runtime-test PROPERTIES
    LABELS "unit"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:temp-runtime-test>"
)

add_executable(task-recovery-test
    unit/test_task_recovery.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/storage/TaskRecovery.cpp
)
target_include_directories(task-recovery-test PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
)
target_link_libraries(task-recovery-test PRIVATE sqlite3)
add_test(NAME task-recovery-test COMMAND task-recovery-test)
set_tests_properties(task-recovery-test PROPERTIES LABELS "unit;storage")

find_package(OpenSSL REQUIRED)
add_executable(llm-provider-config-test
    unit/test_llm_provider_config.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/api/controllers/ConfigController.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/facade/LlmClientFacade.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/LlmClient.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/infrastructure/llm/OpenAICompatibleClient.cpp
    ${CMAKE_SOURCE_DIR}/modules/common/logging/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/modules/common/logging/Logger.cpp
)
target_include_directories(llm-provider-config-test PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
    ${CMAKE_SOURCE_DIR}/third_party/spdlog/include
    ${CMAKE_SOURCE_DIR}/modules
)
target_link_libraries(llm-provider-config-test PRIVATE
    httplib::httplib OpenSSL::SSL OpenSSL::Crypto
)
target_compile_definitions(llm-provider-config-test PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
if(WIN32)
    target_link_libraries(llm-provider-config-test PRIVATE ws2_32)
endif()
add_test(NAME llm-provider-config-test COMMAND llm-provider-config-test)
set_tests_properties(llm-provider-config-test PROPERTIES LABELS "unit;llm")

add_executable(agent-configuration-save-test
    unit/test_agent_configuration_save.cpp
    ${CMAKE_SOURCE_DIR}/backend/agent-server/domain/agent/AgentConfiguration.cpp
)
target_include_directories(agent-configuration-save-test PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/agent-server
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json
)
add_test(NAME agent-configuration-save-test COMMAND agent-configuration-save-test)
set_tests_properties(agent-configuration-save-test PROPERTIES LABELS "unit;agent-config")

if(WIN32)
    add_executable(sqlite-integrity-check
        integration/sqlite_integrity_check.cpp
    )
    target_link_libraries(sqlite-integrity-check PRIVATE sqlite3)

    add_executable(sqlite-migration-test-helper
        integration/sqlite_migration_test_helper.cpp
    )
    target_link_libraries(sqlite-migration-test-helper PRIVATE sqlite3)

    find_program(CODEPILOT_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

    add_test(NAME backend-health-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-health-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 60
    )

    add_test(NAME backend-api-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-api.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-api-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 120
    )

    add_test(NAME backend-sse-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-sse.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -SqliteTestExe "$<TARGET_FILE:sqlite-migration-test-helper>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-sse-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 180
    )

    add_test(NAME backend-concurrency-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-concurrency.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -SqliteCheckExe "$<TARGET_FILE:sqlite-integrity-check>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-concurrency-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 420
    )

    add_test(NAME backend-startup-validation-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-startup.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-startup-validation-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 240
    )

    add_test(NAME backend-sqlite-migration-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-sqlite-migration.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -SqliteTestExe "$<TARGET_FILE:sqlite-migration-test-helper>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-sqlite-migration-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 300
    )

    add_test(NAME backend-llm-failure-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-llm-failure.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-llm-failure-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 300
    )

    add_test(NAME backend-cancellation-regression-test
        COMMAND "${CODEPILOT_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/test-backend-cancellation.ps1"
            -BackendExe "$<TARGET_FILE:codepilot-agent-server>"
            -BuildType "$<CONFIG>"
            -SourceConfigDirectory "${CMAKE_SOURCE_DIR}/config"
    )
    set_tests_properties(backend-cancellation-regression-test PROPERTIES
        LABELS "integration"
        RUN_SERIAL TRUE
        TIMEOUT 300
    )
endif()

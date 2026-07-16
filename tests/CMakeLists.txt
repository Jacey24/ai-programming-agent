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

if(WIN32)
    add_executable(sqlite-integrity-check
        integration/sqlite_integrity_check.cpp
    )
    target_link_libraries(sqlite-integrity-check PRIVATE sqlite3)

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

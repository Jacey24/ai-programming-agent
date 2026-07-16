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
endif()

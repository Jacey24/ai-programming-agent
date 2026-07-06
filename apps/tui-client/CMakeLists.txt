add_executable(codepilot-tui-client
    main.cpp
    TuiApplication.cpp
)

target_include_directories(codepilot-tui-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/frontend
)

install(TARGETS codepilot-tui-client RUNTIME DESTINATION bin)

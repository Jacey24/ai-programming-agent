enable_testing()
find_package(GTest REQUIRED)

add_executable(test_context unit/test_context.cpp)
target_link_libraries(test_context GTest::gtest GTest::gtest_main)

add_executable(test_tool_registry unit/test_tool_registry.cpp)
target_link_libraries(test_tool_registry GTest::gtest GTest::gtest_main)

add_executable(test_permission unit/test_permission.cpp)
target_link_libraries(test_permission GTest::gtest GTest::gtest_main)

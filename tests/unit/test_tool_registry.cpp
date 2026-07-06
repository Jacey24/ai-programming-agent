#include <gtest/gtest.h>
#include "tools/registry.h"
#include "tools/file_tools.h"

TEST(ToolRegistry, RegisterAndCall) {
    ToolRegistry reg;
    reg.register_tool(std::make_unique<FileReadTool>());

    auto schema = reg.generate_tools_schema();
    EXPECT_FALSE(schema.empty());
}

#include <gtest/gtest.h>
#include "core/context.h"

using namespace core;

TEST(ContextManager, AppendAndCount) {
    ContextManager ctx;
    ctx.append({"user", "Hello"});
    ctx.append({"assistant", "Hi there!"});
    auto msgs = ctx.build_context();
    EXPECT_EQ(msgs.size(), 2);
}

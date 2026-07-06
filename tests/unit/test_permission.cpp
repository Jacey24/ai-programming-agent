#include <gtest/gtest.h>
#include "core/permission.h"

using namespace core;

TEST(PermissionController, GrantAndRevoke) {
    PermissionController pc;
    pc.grant("file_read");
    EXPECT_TRUE(pc.check("file_read", "{}"));
    pc.revoke("file_read");
    EXPECT_FALSE(pc.check("file_read", "{}"));
}

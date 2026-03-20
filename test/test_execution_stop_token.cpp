#include <gtest/gtest.h>

#include <execution>

TEST(ExecutionStopTokenTest, InplaceStopCallbackIsInvoked) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    std::inplace_stop_callback cb(token, [&] { called = true; });

    EXPECT_FALSE(called);
    EXPECT_TRUE(src.request_stop());
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, InplaceStopRequestIsIdempotent) {
    std::inplace_stop_source src;
    EXPECT_TRUE(src.request_stop());
    EXPECT_FALSE(src.request_stop());
}


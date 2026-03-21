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

// ── T-4: Expanded stop-token coverage ───────────────────────────────────

TEST(ExecutionStopTokenTest, PostStopCallbackImmediateInvocation) {
    std::inplace_stop_source src;
    src.request_stop();

    bool called = false;
    std::inplace_stop_callback cb(src.get_token(), [&] { called = true; });
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, CallbackAutoDeregisterOnDestruction) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    {
        std::inplace_stop_callback cb(token, [&] { called = true; });
    } // cb destroyed — auto-deregistered
    src.request_stop();
    EXPECT_FALSE(called);
}

TEST(ExecutionStopTokenTest, NeverStopTokenBehavior) {
    std::never_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
    EXPECT_TRUE(token == std::never_stop_token{});
}

TEST(ExecutionStopTokenTest, DefaultConstructedToken) {
    std::inplace_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}


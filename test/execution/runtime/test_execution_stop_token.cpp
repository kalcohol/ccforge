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

// ── T-8: stoppable_token concept probes ──────────────────────────────────

static_assert(std::stoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must satisfy stoppable_token");
static_assert(std::stoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy stoppable_token");

static_assert(std::unstoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy unstoppable_token");
static_assert(!std::unstoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must NOT satisfy unstoppable_token");

static_assert(std::stoppable_token_for<std::inplace_stop_token, void(*)()>,
              "inplace_stop_token must satisfy stoppable_token_for with function pointer");

static_assert(!std::stoppable_token<int>,
              "int must not satisfy stoppable_token");

// ── T-9: sync_wait accessible from std::this_thread ─────────────────────

TEST(ExecutionStopTokenTest, SyncWaitFromThisThread) {
    auto result = std::this_thread::sync_wait(std::execution::just(99));
    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 99);
}


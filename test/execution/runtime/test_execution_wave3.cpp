#include <gtest/gtest.h>
#include <execution>

TEST(AnyStopTokenTest, DefaultNotStopped) {
    std::any_stop_token tok;
    EXPECT_FALSE(tok.stop_requested());
    EXPECT_FALSE(tok.stop_possible());
}

TEST(AnyStopTokenTest, FromInplaceStopSource) {
    std::inplace_stop_source src;
    std::any_stop_token tok{src.get_token()};
    EXPECT_FALSE(tok.stop_requested());
    EXPECT_TRUE(tok.stop_possible());
    src.request_stop();
    EXPECT_TRUE(tok.stop_requested());
}

TEST(AnyStopTokenTest, CopyPreservesState) {
    std::inplace_stop_source src;
    src.request_stop();
    std::any_stop_token tok{src.get_token()};
    std::any_stop_token tok2 = tok;
    EXPECT_TRUE(tok2.stop_requested());
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

struct SimpleTask {
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        SimpleTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

static int g_coro_result = -1;

SimpleTask run_coro() {
    auto tup = co_await std::execution::just(42);
    g_coro_result = std::get<0>(tup);
}

TEST(CoroutineBridgeTest, CoAwaitSender) {
    g_coro_result = -1;
    run_coro();
    EXPECT_EQ(g_coro_result, 42);
}
#endif

#include <gtest/gtest.h>
#include <execution>
#include <type_traits>

namespace {

struct stop_callback_probe {
    void operator()() noexcept {}
};

static_assert(std::stoppable_token<std::any_stop_token>);
static_assert(std::stoppable_token_for<std::any_stop_token, stop_callback_probe>);

} // namespace

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

TEST(AnyStopTokenTest, CallbackTypeInvokesOnStop) {
    std::inplace_stop_source src;
    std::any_stop_token tok{src.get_token()};
    int calls = 0;
    auto cb = [&] { ++calls; };
    std::stop_callback_for_t<std::any_stop_token, decltype(cb)> callback(tok, cb);

    EXPECT_TRUE(src.request_stop());
    EXPECT_EQ(calls, 1);
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#include <forge/task.hpp>
#include <utility>

struct SimpleTask {
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        SimpleTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

struct await_env {};

struct await_env_query {
    int operator()(await_env) const noexcept { return 42; }
};

struct EnvTask {
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        EnvTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}

        friend auto tag_invoke(std::execution::get_env_t, const promise_type&) noexcept
            -> await_env {
            return {};
        }
    };
};

static int g_coro_result = -1;
static int g_env_result = -1;

SimpleTask run_coro() {
    auto tup = co_await std::execution::just(42);
    g_coro_result = std::get<0>(tup);
}

EnvTask run_env_coro() {
    auto tup = co_await std::execution::read_env(await_env_query{});
    g_env_result = std::get<0>(tup);
}

TEST(CoroutineBridgeTest, CoAwaitSender) {
    g_coro_result = -1;
    run_coro();
    EXPECT_EQ(g_coro_result, 42);
}

TEST(CoroutineBridgeTest, CoAwaitSenderSeesPromiseEnv) {
    g_env_result = -1;
    run_env_coro();
    EXPECT_EQ(g_env_result, 42);
}

struct StoppedProbeTask {
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        StoppedProbeTask get_return_object() {
            return StoppedProbeTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept { returned = true; }
        void unhandled_exception() noexcept { errored = true; }
        std::coroutine_handle<> unhandled_stopped() noexcept {
            stopped = true;
            return std::noop_coroutine();
        }

        bool stopped = false;
        bool returned = false;
        bool errored = false;
    };

    explicit StoppedProbeTask(std::coroutine_handle<promise_type> coro) noexcept
        : handle(coro) {}
    StoppedProbeTask(StoppedProbeTask&& other) noexcept
        : handle(std::exchange(other.handle, {})) {}
    StoppedProbeTask(const StoppedProbeTask&) = delete;
    ~StoppedProbeTask() { if (handle) handle.destroy(); }

    std::coroutine_handle<promise_type> handle;
};

StoppedProbeTask run_stopped_probe() {
    co_await std::execution::just_stopped();
}

TEST(CoroutineBridgeTest, StoppedSenderCallsPromiseUnhandledStopped) {
    auto task = run_stopped_probe();
    ASSERT_TRUE(task.handle);
    auto& promise = task.handle.promise();
    EXPECT_TRUE(promise.stopped);
    EXPECT_FALSE(promise.returned);
    EXPECT_FALSE(promise.errored);
}

forge::task<int> stopped_int_task() {
    co_await std::execution::just_stopped();
    co_return 7;
}

forge::task<void> stopped_void_task() {
    co_await std::execution::just_stopped();
}

TEST(CoroutineBridgeTest, ForgeTaskPropagatesStoppedCompletion) {
    auto int_result = std::execution::sync_wait(stopped_int_task());
    EXPECT_FALSE(int_result.has_value());

    auto void_result = std::execution::sync_wait(stopped_void_task());
    EXPECT_FALSE(void_result.has_value());
}
#endif

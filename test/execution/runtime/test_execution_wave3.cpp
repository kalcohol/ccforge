#include <gtest/gtest.h>
#include <execution>
#include <type_traits>

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
static int g_multi_arg_result = -1;
static bool g_void_result = false;
static int g_ordinary_awaitable_result = -1;
static int g_sender_awaiter_result = -1;
static int g_member_co_await_result = -1;
static int g_free_co_await_result = -1;
static int g_member_as_awaitable_result = -1;
static int g_adapted_as_awaitable_result = -1;

struct immediate_int_awaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 17; }
};

template<int Value>
struct immediate_value_awaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return Value; }
};

struct sender_awaiter {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 23; }
};

struct member_co_await_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
    auto operator co_await() && noexcept -> immediate_value_awaiter<29> {
        return {};
    }
};

struct free_co_await_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

auto operator co_await(free_co_await_sender&&) noexcept
    -> immediate_value_awaiter<31> {
    return {};
}

struct member_as_awaitable_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class Promise>
    auto as_awaitable(Promise&) && noexcept -> immediate_value_awaiter<37> {
        return {};
    }

    auto operator co_await() && noexcept -> immediate_value_awaiter<-1> {
        return {};
    }
};

struct completion_await_adaptor {
    struct adapted {
        template<class Promise>
        auto as_awaitable(Promise&) && noexcept -> immediate_value_awaiter<41> {
            return {};
        }
    };

    template<class Sender>
    auto operator()(Sender&&) const noexcept -> adapted {
        return {};
    }
};

struct completion_await_env {
    auto query(std::execution::get_await_completion_adaptor_t) const noexcept
        -> completion_await_adaptor {
        return {};
    }
};

struct completion_adapted_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> completion_await_env { return {}; }
};

SimpleTask run_coro() {
    auto value = co_await std::execution::just(42);
    static_assert(std::is_same_v<decltype(value), int>);
    g_coro_result = value;
}

SimpleTask run_ordinary_awaitable_coro() {
    g_ordinary_awaitable_result = co_await immediate_int_awaiter{};
}

SimpleTask run_sender_awaiter_coro() {
    g_sender_awaiter_result = co_await sender_awaiter{};
}

SimpleTask run_member_co_await_coro() {
    g_member_co_await_result = co_await member_co_await_sender{};
}

SimpleTask run_free_co_await_coro() {
    g_free_co_await_result = co_await free_co_await_sender{};
}

SimpleTask run_member_as_awaitable_coro() {
    g_member_as_awaitable_result = co_await member_as_awaitable_sender{};
}

SimpleTask run_adapted_as_awaitable_coro() {
    g_adapted_as_awaitable_result = co_await completion_adapted_sender{};
}

EnvTask run_env_coro() {
    auto value = co_await std::execution::read_env(await_env_query{});
    static_assert(std::is_same_v<decltype(value), int>);
    g_env_result = value;
}

SimpleTask run_void_coro() {
    co_await std::execution::just();
    g_void_result = true;
}

SimpleTask run_multi_arg_coro() {
    auto value = co_await std::execution::just(3, 4);
    static_assert(std::is_same_v<decltype(value), std::tuple<int, int>>);
    g_multi_arg_result = std::get<0>(value) + std::get<1>(value);
}

template<class R>
struct multi_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    bool use_double = false;

    void start() & noexcept {
        if (use_double) {
            std::execution::set_value(std::move(rcvr), 4.5);
        } else {
            std::execution::set_value(std::move(rcvr), 3);
        }
    }
};

struct multi_value_sender {
    using sender_concept = std::execution::sender_t;

    bool use_double = false;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> multi_value_op<R> {
        return multi_value_op<R>{std::move(r), use_double};
    }
};

template<class R>
struct empty_or_int_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    bool use_empty = false;

    void start() & noexcept {
        if (use_empty) {
            std::execution::set_value(std::move(rcvr));
        } else {
            std::execution::set_value(std::move(rcvr), 8);
        }
    }
};

struct empty_or_int_sender {
    using sender_concept = std::execution::sender_t;

    bool use_empty = false;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> empty_or_int_op<R> {
        return empty_or_int_op<R>{std::move(r), use_empty};
    }
};

using multi_value_as_awaitable_t = decltype(std::execution::as_awaitable(
    std::declval<multi_value_sender>(),
    std::declval<SimpleTask::promise_type&>()));
using empty_or_int_as_awaitable_t = decltype(std::execution::as_awaitable(
    std::declval<empty_or_int_sender>(),
    std::declval<SimpleTask::promise_type&>()));

static_assert(std::same_as<multi_value_as_awaitable_t, multi_value_sender&&>);
static_assert(std::same_as<empty_or_int_as_awaitable_t, empty_or_int_sender&&>);
static_assert(noexcept(std::execution::as_awaitable(
    std::declval<immediate_int_awaiter>(),
    std::declval<SimpleTask::promise_type&>())));
static_assert(noexcept(std::execution::as_awaitable(
    std::declval<member_as_awaitable_sender>(),
    std::declval<SimpleTask::promise_type&>())));

TEST(CoroutineBridgeTest, CoAwaitSender) {
    g_coro_result = -1;
    run_coro();
    EXPECT_EQ(g_coro_result, 42);
}

TEST(CoroutineBridgeTest, CoAwaitOrdinaryAwaitable) {
    g_ordinary_awaitable_result = -1;
    run_ordinary_awaitable_coro();
    EXPECT_EQ(g_ordinary_awaitable_result, 17);
}

TEST(CoroutineBridgeTest, SenderAwaiterPassesThroughBeforeSenderBridge) {
    g_sender_awaiter_result = -1;
    run_sender_awaiter_coro();
    EXPECT_EQ(g_sender_awaiter_result, 23);
}

TEST(CoroutineBridgeTest, SenderMemberCoAwaitPassesThroughBeforeSenderBridge) {
    g_member_co_await_result = -1;
    run_member_co_await_coro();
    EXPECT_EQ(g_member_co_await_result, 29);
}

TEST(CoroutineBridgeTest, SenderFreeCoAwaitPassesThroughBeforeSenderBridge) {
    g_free_co_await_result = -1;
    run_free_co_await_coro();
    EXPECT_EQ(g_free_co_await_result, 31);
}

TEST(CoroutineBridgeTest, MemberAsAwaitablePrecedesOrdinaryCoAwait) {
    g_member_as_awaitable_result = -1;
    run_member_as_awaitable_coro();
    EXPECT_EQ(g_member_as_awaitable_result, 37);
}

TEST(CoroutineBridgeTest, CompletionAdaptorCanProvideAsAwaitable) {
    g_adapted_as_awaitable_result = -1;
    run_adapted_as_awaitable_coro();
    EXPECT_EQ(g_adapted_as_awaitable_result, 41);
}

TEST(CoroutineBridgeTest, CoAwaitSenderSeesPromiseEnv) {
    g_env_result = -1;
    run_env_coro();
    EXPECT_EQ(g_env_result, 42);
}

TEST(CoroutineBridgeTest, CoAwaitZeroValueSenderReturnsVoid) {
    g_void_result = false;
    run_void_coro();
    EXPECT_TRUE(g_void_result);
}

TEST(CoroutineBridgeTest, CoAwaitMultiArgumentValueReturnsTuple) {
    g_multi_arg_result = -1;
    run_multi_arg_coro();
    EXPECT_EQ(g_multi_arg_result, 7);
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

struct DefaultStoppedTask {
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        DefaultStoppedTask get_return_object() noexcept {
            return DefaultStoppedTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    struct awaiter {
        explicit awaiter(handle_t child) noexcept : child(child) {}
        awaiter(awaiter&& other) noexcept
            : child(std::exchange(other.child, {})) {}
        awaiter(const awaiter&) = delete;
        ~awaiter() {
            if (child) {
                child.destroy();
            }
        }

        bool await_ready() const noexcept { return false; }

        template<class ParentPromise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<ParentPromise> parent) noexcept {
            child.promise().set_continuation(parent);
            return child;
        }

        void await_resume() const noexcept {}

        handle_t child;
    };

    explicit DefaultStoppedTask(handle_t handle) noexcept : handle(handle) {}
    DefaultStoppedTask(DefaultStoppedTask&& other) noexcept
        : handle(std::exchange(other.handle, {})) {}
    DefaultStoppedTask(const DefaultStoppedTask&) = delete;
    ~DefaultStoppedTask() {
        if (handle) {
            handle.destroy();
        }
    }

    auto operator co_await() && noexcept -> awaiter {
        return awaiter{std::exchange(handle, {})};
    }

    handle_t handle;
};

StoppedProbeTask run_stopped_probe() {
    co_await std::execution::just_stopped();
}

DefaultStoppedTask default_stopped_task() {
    co_await std::execution::just_stopped();
}

StoppedProbeTask run_nested_stopped_probe() {
    co_await default_stopped_task();
}

TEST(CoroutineBridgeTest, StoppedSenderCallsPromiseUnhandledStopped) {
    auto task = run_stopped_probe();
    ASSERT_TRUE(task.handle);
    auto& promise = task.handle.promise();
    EXPECT_TRUE(promise.stopped);
    EXPECT_FALSE(promise.returned);
    EXPECT_FALSE(promise.errored);
}

TEST(CoroutineBridgeTest, DefaultUnhandledStoppedPropagatesToContinuation) {
    auto task = run_nested_stopped_probe();
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

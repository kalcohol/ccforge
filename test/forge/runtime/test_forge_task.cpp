#include <gtest/gtest.h>
#include <forge/task.hpp>
#include <forge/static_thread_pool.hpp>
#include <execution>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

template<class Pred>
bool wait_until(Pred pred) {
    for (int i = 0; i < 200; ++i) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

struct task_marker_error {};

struct deref_unique {
    int operator()(std::unique_ptr<int> value) const noexcept {
        return *value;
    }
};

struct pending_state {
    std::atomic<bool> started{false};
    std::atomic<bool> destroyed{false};
};

template<class R>
struct pending_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    std::shared_ptr<pending_state> state;

    ~pending_op() {
        state->destroyed.store(true, std::memory_order_release);
    }

    void start() & noexcept {
        state->started.store(true, std::memory_order_release);
    }
};

struct pending_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<pending_state> state;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> pending_op<R> {
        return pending_op<R>{std::move(r), std::move(state)};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> pending_op<R> {
        return pending_op<R>{std::move(r), state};
    }
};

struct recording_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::atomic<bool>* completed = nullptr;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {
        completed->store(true, std::memory_order_release);
    }

    template<class E>
    void set_error(E&&) && noexcept {
        completed->store(true, std::memory_order_release);
    }

    void set_stopped() && noexcept {
        completed->store(true, std::memory_order_release);
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

template<class R>
struct never_started_op {
    using operation_state_concept = std::execution::operation_state_t;
    void start() & noexcept {}
};

struct throwing_connect_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R) && -> never_started_op<R> {
        throw std::runtime_error{"connect failed"};
    }
};

struct inline_completion_state {
    bool start_returned = false;
    bool resumed_before_start_returned = false;
};

template<class R>
struct inline_completion_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    inline_completion_state* state = nullptr;

    void start() & noexcept {
        std::execution::set_value(std::move(rcvr));
        state->start_returned = true;
    }
};

struct inline_completion_sender {
    using sender_concept = std::execution::sender_t;

    inline_completion_state* state = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> inline_completion_op<R> {
        return inline_completion_op<R>{std::move(rcvr), state};
    }
};

} // namespace

forge::task<int> simple_task() {
    co_return 42;
}

forge::task<int> await_just_task() {
    auto value = co_await std::execution::just(41);
    co_return value + 1;
}

forge::task<int> await_move_only_lvalue_sender_task() {
    auto sndr = std::execution::just(std::make_unique<int>(53))
        | std::execution::then(deref_unique{});
    auto value = co_await std::move(sndr);
    co_return value;
}

forge::task<void> void_task(int* result) {
    *result = 77;
    co_return;
}

forge::task<int> await_run_loop_task(
    std::execution::run_loop::scheduler scheduler,
    std::atomic<int>* phase) {
    phase->store(1, std::memory_order_release);
    co_await std::execution::schedule(scheduler);
    phase->store(2, std::memory_order_release);
    co_return 99;
}

forge::task<int> await_thread_pool_task(forge::static_thread_pool::scheduler scheduler) {
    co_await std::execution::schedule(scheduler);
    co_return 123;
}

forge::task<int> await_error_task() {
    co_await std::execution::just_error(task_marker_error{});
    co_return 1;
}

forge::task<int> await_stopped_task() {
    co_await std::execution::just_stopped();
    co_return 1;
}

forge::task<void> await_inline_completion_task(inline_completion_state* state) {
    co_await inline_completion_sender{state};
    state->resumed_before_start_returned = !state->start_returned;
}

forge::task<void> await_pending_task(std::shared_ptr<pending_state> state) {
    co_await pending_sender{std::move(state)};
}

forge::task<int> await_throwing_connect_task() {
    co_await throwing_connect_sender{};
    co_return 1;
}

TEST(TaskTest, IntTask) {
    auto t = simple_task();
    auto result = std::execution::sync_wait(std::move(t));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(TaskTest, CoAwaitJustStillWorks) {
    auto result = std::execution::sync_wait(await_just_task());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(TaskTest, InlineSenderResumesAfterStartReturns) {
    inline_completion_state state;

    auto result = std::execution::sync_wait(await_inline_completion_task(&state));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(state.start_returned);
    EXPECT_FALSE(state.resumed_before_start_returned);
}

TEST(TaskTest, CoAwaitNonCopyableLvalueSenderRequiresMove) {
    auto result = std::execution::sync_wait(await_move_only_lvalue_sender_task());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 53);
}

TEST(TaskTest, VoidTask) {
    int val = 0;
    auto t = void_task(&val);
    std::execution::sync_wait(std::move(t));
    EXPECT_EQ(val, 77);
}

TEST(TaskTest, CoAwaitRunLoopSuspendsUntilLoopRuns) {
    std::execution::run_loop loop;
    std::atomic<int> phase{0};
    std::atomic<bool> completed{false};
    std::optional<std::tuple<int>> result;
    std::exception_ptr failure;

    auto task = await_run_loop_task(loop.get_scheduler(), &phase);
    std::thread waiter{[&] {
        try {
            result = std::execution::sync_wait(std::move(task));
            completed.store(true, std::memory_order_release);
        } catch (...) {
            failure = std::current_exception();
            completed.store(true, std::memory_order_release);
        }
    }};

    ASSERT_TRUE(wait_until([&] {
        return phase.load(std::memory_order_acquire) == 1;
    }));
    EXPECT_FALSE(completed.load(std::memory_order_acquire));

    std::thread runner{[&] { loop.run(); }};
    ASSERT_TRUE(wait_until([&] {
        return phase.load(std::memory_order_acquire) == 2;
    }));
    loop.finish();
    runner.join();
    waiter.join();

    if (failure) {
        std::rethrow_exception(failure);
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
}

TEST(TaskTest, CoAwaitStaticThreadPoolResumesAsynchronously) {
    forge::static_thread_pool pool{1};

    auto result = std::execution::sync_wait(
        await_thread_pool_task(pool.get_scheduler()));
    pool.wait();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 123);
}

TEST(TaskTest, ReceiverStopTokenReachesAwaitedSender) {
    forge::static_thread_pool pool{1};
    std::inplace_stop_source source;
    source.request_stop();
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, source.get_token()));

    auto result = std::execution::sync_wait(
        std::execution::write_env(
            await_thread_pool_task(pool.get_scheduler()), env));
    pool.wait();

    EXPECT_FALSE(result.has_value());
}

TEST(TaskTest, ConnectingMovedFromTaskThrows) {
    auto empty = simple_task();
    auto live = std::move(empty);

    EXPECT_THROW(
        (void)std::execution::sync_wait(std::move(empty)),
        std::logic_error);

    auto result = std::execution::sync_wait(std::move(live));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(TaskTest, CoAwaitErrorPropagates) {
    EXPECT_THROW((void)std::execution::sync_wait(await_error_task()), task_marker_error);
}

TEST(TaskTest, CoAwaitStoppedPropagates) {
    auto result = std::execution::sync_wait(await_stopped_task());

    EXPECT_FALSE(result.has_value());
}

TEST(TaskTest, CoAwaitThrowingConnectPropagatesException) {
    EXPECT_THROW(
        (void)std::execution::sync_wait(await_throwing_connect_task()),
        std::runtime_error);
}

TEST(TaskTest, DestroyingStartedTaskDestroysAwaitedOperation) {
    auto state = std::make_shared<pending_state>();
    std::atomic<bool> completed{false};

    {
        auto task = await_pending_task(state);
        auto op = std::execution::connect(std::move(task), recording_receiver{&completed});
        std::execution::start(op);

        EXPECT_TRUE(state->started.load(std::memory_order_acquire));
        EXPECT_FALSE(completed.load(std::memory_order_acquire));
    }

    EXPECT_TRUE(state->destroyed.load(std::memory_order_acquire));
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
}

#else
TEST(TaskTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}
#endif

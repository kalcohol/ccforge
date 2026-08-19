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

// task<T> supports throwing-move T: __complete hands the stored result to
// the receiver by reference, and receivers that materialize the value (e.g.
// sync_wait) do so inside their own try/catch, routing a throw to the error
// channel. The move allowance below lets the co_return-to-variant move
// succeed and makes the receiver-side move throw.
struct throwing_move_result {
    explicit throwing_move_result(int allowed) noexcept
        : moves_allowed(allowed) {}

    throwing_move_result(throwing_move_result&& other)
        : moves_allowed(other.moves_allowed - 1) {
        if (other.moves_allowed <= 0) {
            throw task_marker_error{};
        }
    }

    throwing_move_result(const throwing_move_result&) = delete;
    auto operator=(const throwing_move_result&)
        -> throwing_move_result& = delete;
    auto operator=(throwing_move_result&&)
        -> throwing_move_result& = delete;

    int moves_allowed;
};

forge::task<std::thread::id> await_scope_join_task(
    std::execution::simple_counting_scope* scope) {
    co_await scope->join();
    co_return std::this_thread::get_id();
}

forge::task<void> await_scope_join_record_thread_task(
    std::execution::simple_counting_scope* scope,
    std::atomic<std::thread::id>* resumed_on) {
    co_await scope->join();
    resumed_on->store(std::this_thread::get_id(), std::memory_order_release);
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

TEST(TaskTest, CoAwaitDrainedScopeJoinCompletesInsideTask) {
    std::execution::simple_counting_scope scope;

    auto result = std::execution::sync_wait(await_scope_join_task(&scope));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), std::this_thread::get_id());
}

TEST(TaskTest, CoAwaitPendingScopeJoinResumesOnStartScheduler) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());
    auto assoc = std::make_shared<std::optional<association_t>>(
        token.try_associate());
    ASSERT_TRUE(assoc->has_value());

    std::thread releaser{[assoc] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assoc->reset();
    }};

    // sync_wait's environment advertises its run_loop as the start
    // scheduler; the task forwards it, so join's completion must hop back
    // onto the loop drained by this thread instead of resuming the
    // coroutine on the releasing thread.
    auto result = std::execution::sync_wait(await_scope_join_task(&scope));
    releaser.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), std::this_thread::get_id());
}

TEST(TaskTest, ScopeJoinWithoutStartSchedulerFallsBackToInlineCompletion) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());
    auto assoc = std::make_shared<std::optional<association_t>>(
        token.try_associate());
    ASSERT_TRUE(assoc->has_value());

    std::atomic<bool> completed{false};
    std::atomic<std::thread::id> resumed_on{};

    auto task = await_scope_join_record_thread_task(&scope, &resumed_on);
    auto op = std::execution::connect(
        std::move(task), recording_receiver{&completed});
    std::execution::start(op);
    EXPECT_FALSE(completed.load(std::memory_order_acquire));

    std::atomic<std::thread::id> releaser_id{};
    std::thread releaser{[&] {
        releaser_id.store(
            std::this_thread::get_id(), std::memory_order_release);
        assoc->reset();
    }};
    releaser.join();

    // recording_receiver has an empty environment, so the task supplies the
    // inline fallback scheduler and the join completion stays on the thread
    // that released the last association.
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(
        resumed_on.load(std::memory_order_acquire),
        releaser_id.load(std::memory_order_acquire));
}

TEST(TaskTest, ThrowingMoveResultRoutesToErrorChannel) {
    auto make = [](int allowed) -> forge::task<throwing_move_result> {
        co_return throwing_move_result{allowed};
    };

    // Allowance 1: the move into the promise variant succeeds, the move
    // inside sync_wait's receiver throws and lands in the error channel.
    EXPECT_THROW(
        (void)std::execution::sync_wait(make(1)),
        task_marker_error);

    // A generous allowance completes normally, proving task<T> accepts and
    // delivers potentially-throwing-move result types.
    auto result = std::execution::sync_wait(make(8));
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(std::get<0>(*result).moves_allowed, 0);
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

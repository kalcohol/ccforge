#include "../compile/awaitable_conformance_types.hpp"
#include <forge/any_sender.hpp>
#include <forge/async_scope.hpp>
#include <forge/erased_sender.hpp>
#include <forge/wait_result.hpp>
#include <forge/io/coro.hpp>
#include <forge/resource_context.hpp>
#include <forge/start_detached.hpp>
#include <forge/task.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace {
namespace ex = std::execution;
namespace a = awaitable_tests;

struct destroyed_state {
    void* operation = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    int completion = 0;
    int value = 0;
    std::exception_ptr error;
    bool source_destroyed = false;
    int completions = 0;
};
struct destroying_receiver {
    using receiver_concept = ex::receiver_tag;
    destroyed_state* state;
    void finish(int kind) noexcept {
        auto* saved = state;
        saved->completion = kind;
        ++saved->completions;
        auto* operation = std::exchange(saved->operation, nullptr);
        auto destroy = saved->destroy;
        destroy(operation);
        // The receiver and completion arguments have both been destroyed.
    }
    void set_value(int value) && noexcept {
        state->value = value;
        finish(1);
    }
    void set_value() && noexcept { finish(1); }
    void set_error(std::exception_ptr error) && noexcept {
        state->error = std::move(error);
        finish(2);
    }
    void set_stopped() && noexcept { finish(3); }
    auto operator&() -> destroying_receiver* = delete;
};
struct source_guard {
    bool* destroyed;
    ~source_guard() { *destroyed = true; }
};
struct owned_int : a::int_awaiter {
    std::unique_ptr<source_guard> guard;
    explicit owned_int(bool* flag) : guard(new source_guard{flag}) {}
};
struct owned_void : a::void_awaiter {
    std::unique_ptr<source_guard> guard;
    explicit owned_void(bool* flag) : guard(new source_guard{flag}) {}
};
struct owned_error : owned_int {
    using owned_int::owned_int;
    int await_resume() const { throw std::runtime_error("await result"); }
};
struct owned_suspend_error : owned_int {
    using owned_int::owned_int;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const { throw std::runtime_error("await suspend"); }
};
struct owned_stopped : owned_int {
    using owned_int::owned_int;
    bool await_ready() const noexcept { return false; }
    template<class P>
    auto await_suspend(std::coroutine_handle<P> h) const noexcept -> std::coroutine_handle<> {
        return h.promise().unhandled_stopped();
    }
};
struct owned_async : owned_int {
    std::coroutine_handle<>* suspended;
    owned_async(bool* flag, std::coroutine_handle<>* h) : owned_int(flag), suspended(h) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const noexcept { *suspended = h; }
};

template<class S>
void start_destroying(S source, destroyed_state& state) {
    using op_t = ex::connect_result_t<S, destroying_receiver>;
    state.destroy = [](void* p) noexcept { delete static_cast<op_t*>(p); };
    auto* op = new op_t(ex::connect(std::move(source), destroying_receiver{&state}));
    state.operation = op;
    ex::start(*op);
}

TEST(AwaitableSender, EveryCompletionCanDestroyTheSuspendedFrame) {
    destroyed_state value;
    start_destroying(owned_int{&value.source_destroyed}, value);
    EXPECT_EQ(value.completion, 1);
    EXPECT_EQ(value.value, 17);
    EXPECT_TRUE(value.source_destroyed);
    EXPECT_EQ(value.operation, nullptr);

    destroyed_state empty;
    start_destroying(owned_void{&empty.source_destroyed}, empty);
    EXPECT_EQ(empty.completion, 1);
    EXPECT_TRUE(empty.source_destroyed);

    destroyed_state error;
    start_destroying(owned_error{&error.source_destroyed}, error);
    EXPECT_EQ(error.completion, 2);
    EXPECT_TRUE(error.source_destroyed);
    EXPECT_THROW(std::rethrow_exception(error.error), std::runtime_error);

    destroyed_state suspend_error;
    start_destroying(owned_suspend_error{&suspend_error.source_destroyed}, suspend_error);
    EXPECT_EQ(suspend_error.completion, 2);
    EXPECT_TRUE(suspend_error.source_destroyed);
    EXPECT_THROW(std::rethrow_exception(suspend_error.error), std::runtime_error);

    destroyed_state stopped;
    start_destroying(owned_stopped{&stopped.source_destroyed}, stopped);
    EXPECT_EQ(stopped.completion, 3);
    EXPECT_TRUE(stopped.source_destroyed);
}

TEST(AwaitableSender, SyncWaitTranslatesExceptionAndStopped) {
    bool destroyed = false;
    EXPECT_THROW(forge::sync_wait(owned_error{&destroyed}), std::runtime_error);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_THROW(forge::sync_wait(owned_suspend_error{&destroyed}), std::runtime_error);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_FALSE(forge::sync_wait(owned_stopped{&destroyed}));
    EXPECT_TRUE(destroyed);
}

TEST(AwaitableSender, AnotherAgentCanCompleteAndDestroyTheFrame) {
    destroyed_state state;
    std::coroutine_handle<> suspended;
    start_destroying(owned_async{&state.source_destroyed, &suspended}, state);
    ASSERT_TRUE(suspended);
    EXPECT_EQ(state.completion, 0);
    std::thread worker([suspended] { suspended.resume(); });
    worker.join();
    EXPECT_EQ(state.completion, 1);
    EXPECT_EQ(state.value, 17);
    EXPECT_TRUE(state.source_destroyed);
}

TEST(AwaitableSender, ReferenceAndTransformedResultsUseTheFallback) {
    int value = 42;
    struct ref_receiver {
        using receiver_concept = ex::receiver_tag;
        int** observed;
        void set_value(int& result) && noexcept { *observed = std::addressof(result); }
        void set_error(std::exception_ptr) && noexcept { std::terminate(); }
        void set_stopped() && noexcept { std::terminate(); }
    };
    int* observed = nullptr;
    auto reference_op = ex::connect(a::ref_awaiter{&value}, ref_receiver{&observed});
    ex::start(reference_op);
    EXPECT_EQ(observed, &value);
    destroyed_state transformed;
    start_destroying(a::raw_sender{}, transformed);
    EXPECT_EQ(transformed.completion, 1);
    EXPECT_EQ(transformed.value, 17);
    struct start_receiver : destroying_receiver {
        auto get_env() const noexcept -> a::start_env { return {}; }
    };
    destroyed_state started;
    using start_op = ex::connect_result_t<a::start_sender, start_receiver>;
    started.destroy = [](void* p) noexcept { delete static_cast<start_op*>(p); };
    auto* op = new start_op(ex::connect(a::start_sender{}, start_receiver{{&started}}));
    started.operation = op;
    ex::start(*op);
    EXPECT_EQ(started.completion, 1);
    EXPECT_EQ(started.completions, 1);
    EXPECT_EQ(started.value, 17);
    auto member = forge::sync_wait(a::member_awaitable{});
    auto free = forge::sync_wait(a::free_awaitable{});
    auto adapted = forge::sync_wait(a::member_adapted{});
    ASSERT_TRUE(member && free && adapted);
    EXPECT_EQ(std::get<0>(*member), 17);
    EXPECT_EQ(std::get<0>(*free), 17);
    EXPECT_EQ(std::get<0>(*adapted), 17);
}

TEST(AwaitableSender, InternalCompletionAwaitExcludesReceiverAndValueAdl) {
    for (bool fail : {false, true}) {
        awaitable_adl::result output;
        auto op = ex::connect(a::adl_value_awaiter{fail}, awaitable_adl::receiver{&output});
        ex::start(op);
        EXPECT_EQ(output.completion, fail ? 2 : 1);
        if (fail) {
            EXPECT_THROW(std::rethrow_exception(output.error), std::runtime_error);
        } else {
            EXPECT_EQ(output.number, 61);
        }
    }
}

TEST(AwaitableSender, MovingAnUnstartedOperationTransfersFrameOwnership) {
    destroyed_state state;
    {
        auto original = ex::connect(owned_int{&state.source_destroyed}, a::receiver{});
        auto moved = std::move(original);
        EXPECT_FALSE(state.source_destroyed);
        ex::start(moved);
    }
    EXPECT_TRUE(state.source_destroyed);
}

template<class S>
forge::task<void> await_without_fallthrough(S source, bool* after_await) {
    co_await std::move(source);
    *after_await = true;
}

TEST(AwaitableSender, ErrorAndStoppedNeverReachCodeAfterAwait) {
    bool destroyed = false;
    bool after_await = false;
    EXPECT_THROW(forge::sync_wait(await_without_fallthrough(
        owned_error{&destroyed}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_THROW(forge::sync_wait(await_without_fallthrough(
        owned_suspend_error{&destroyed}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_FALSE(forge::sync_wait(await_without_fallthrough(
        owned_stopped{&destroyed}, &after_await)));
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    EXPECT_THROW(forge::sync_wait(await_without_fallthrough(
        a::adl_value_awaiter{true}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
}

TEST(AwaitableSender, LockedAlgorithmsComposeThroughUnchangedCpos) {
    auto combined = forge::sync_wait(ex::when_all(a::int_awaiter{}, ex::just(25)));
    ASSERT_TRUE(combined);
    EXPECT_EQ(*combined, std::make_tuple(17, 25));
    auto cached = forge::split(a::int_awaiter{});
    EXPECT_EQ(std::get<0>(*forge::sync_wait(cached)), 17);
    auto rvalue_only = forge::split(a::member_awaitable{});
    EXPECT_EQ(std::get<0>(*forge::sync_wait(rvalue_only)), 17);
    EXPECT_EQ(std::get<0>(*forge::sync_wait(cached)), 17);
    ex::counting_scope scope;
    auto wrapped = forge::sync_wait(scope.get_token().wrap(a::int_awaiter{}));
    ASSERT_TRUE(wrapped);
    EXPECT_EQ(std::get<0>(*wrapped), 17);
    auto standard = std::this_thread::sync_wait(a::int_awaiter{});
    ASSERT_TRUE(standard);
    EXPECT_EQ(std::get<0>(*standard), 17);
    auto associated = forge::sync_wait(ex::associate(a::int_awaiter{}, scope.get_token()));
    ASSERT_TRUE(associated);
    EXPECT_EQ(std::get<0>(*associated), 17);
    auto future = ex::spawn_future(a::int_awaiter{}, scope.get_token());
    auto result = forge::sync_wait(std::move(future));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 17);
    EXPECT_TRUE(forge::sync_wait(scope.join()));
}

struct await_scheduler;
struct await_schedule_env {
    int* calls;
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept -> await_scheduler;
};
struct await_schedule_sender {
    int* calls;
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept { ++*calls; }
    auto get_env() const noexcept -> await_schedule_env { return {calls}; }
};
struct await_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int* calls;
    auto schedule() const noexcept -> await_schedule_sender { return {calls}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept -> ex::forward_progress_guarantee {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const await_scheduler&) const noexcept = default;
};
auto await_schedule_env::query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept
    -> await_scheduler { return {calls}; }
static_assert(ex::scheduler<await_scheduler>);
struct join_env {
    await_scheduler scheduler;
    auto query(ex::get_start_scheduler_t) const noexcept -> await_scheduler { return scheduler; }
};
struct join_receiver {
    using receiver_concept = ex::receiver_tag;
    int* completions;
    await_scheduler scheduler;
    void set_value() && noexcept { ++*completions; }
    void set_error(std::exception_ptr) && noexcept { std::terminate(); }
    void set_stopped() && noexcept { std::terminate(); }
    auto get_env() const noexcept -> join_env { return {scheduler}; }
};
template<class Scope>
void check_join_with_awaitable_schedule() {
    for (bool pending : {false, true}) {
        Scope scope;
        using association = decltype(scope.get_token().try_associate());
        std::optional<association> held;
        if (pending) held.emplace(scope.get_token().try_associate());
        int completions = 0;
        int schedule_calls = 0;
        auto op = ex::connect(scope.join(), join_receiver{&completions, {&schedule_calls}});
        ex::start(op);
        EXPECT_EQ(completions, pending ? 0 : 1);
        held.reset();
        EXPECT_EQ(completions, 1);
        // Empty join still constructs the schedule operation but never starts it.
        EXPECT_EQ(schedule_calls, pending ? 1 : 0);
    }
}
TEST(AwaitableSender, BothScopesConnectAwaitableSchedulesBeforeEitherJoinPath) {
    check_join_with_awaitable_schedule<ex::simple_counting_scope>();
    check_join_with_awaitable_schedule<ex::counting_scope>();
}

struct recording_awaiter : a::void_awaiter {
    std::atomic<int>* count;
    explicit recording_awaiter(std::atomic<int>* value) : count(value) {}
    void await_resume() const noexcept { ++*count; }
};
forge::task<int> task_awaits_ordinary() { co_return co_await a::int_awaiter{}; }
forge::io::io_task<int> io_awaits_sender() {
    auto [value] = co_await forge::io::await_sender(a::int_awaiter{});
    co_return value;
}
TEST(AwaitableSender, ForgeEntryPointsAcceptCompatibleAwaitables) {
    std::atomic<int> count{0};
    forge::start_detached(recording_awaiter{&count});
    forge::async_scope scope;
    EXPECT_TRUE(scope.spawn(recording_awaiter{&count}));
    scope.wait();
    forge::resource_context context{1};
    EXPECT_TRUE(context.spawn(recording_awaiter{&count}));
    context.wait();
    EXPECT_EQ(count.load(), 3);
    forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>> erased{a::int_awaiter{}};
    auto result = erased.sync_wait();
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 17);
    forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>>
        lvalue_only{a::lvalue_sender{}};
    auto lvalue_result = lvalue_only.sync_wait();
    ASSERT_TRUE(lvalue_result);
    EXPECT_EQ(std::get<0>(*lvalue_result), 29);
    // Borrowing is repeatable, rather than consuming the stored source.
    EXPECT_EQ(std::get<0>(*lvalue_only.sync_wait()), 29);
    auto outcome = forge::wait_result(a::int_awaiter{});
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(std::get<0>(outcome.value()), 17);
    using erased_sigs = ex::completion_signatures<ex::set_value_t(int),
        ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;
    forge::erased_sender<erased_sigs> erased_source{a::int_awaiter{}};
    auto erased_result = forge::sync_wait(std::move(erased_source));
    ASSERT_TRUE(erased_result);
    EXPECT_EQ(std::get<0>(*erased_result), 17);
    auto task_result = forge::sync_wait(task_awaits_ordinary());
    ASSERT_TRUE(task_result);
    EXPECT_EQ(std::get<0>(*task_result), 17);
    auto io_result = forge::sync_wait(forge::io::as_sender(io_awaits_sender()));
    ASSERT_TRUE(io_result);
    EXPECT_EQ(std::get<0>(*io_result), 17);
}

struct local_query {
    template<class Env>
    auto operator()(const Env& env) const noexcept -> decltype(env.query(*this)) {
        return env.query(*this);
    }
};
struct local_environment {
    int query(local_query) const noexcept { return 42; }
};
struct environment_task {
    struct promise_type : ex::with_awaitable_senders<promise_type> {
        int* output;
        explicit promise_type(int* result) noexcept : output(result) {}
        auto operator&() -> promise_type* = delete;
        environment_task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        auto get_env() const noexcept -> local_environment { return {}; }
    };
};
environment_task read_non_forwarding_query(int* result) {
    *result = co_await ex::read_env(local_query{});
}
TEST(AwaitableSender, ReceiverKeepsPromiseEnvironmentLatitude) {
    int result = 0;
    read_non_forwarding_query(&result);
    EXPECT_EQ(result, 42);
}

TEST(AwaitableSender, PreparedMemberNoexceptIsUsable) {
    a::promise promise;
    auto awaiter = ex::as_awaitable(a::adapted_sender<true, true>{}, promise);
    EXPECT_EQ(awaiter.await_resume(), 17);
}
} // namespace

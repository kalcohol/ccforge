#include "owned_awaiters.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <execution>
#include <memory>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace bridge_intermediate_adl {
struct transformed {
    using sender_concept = std::execution::sender_tag;
    awaitable_tests::lvalue_sender member;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> { return {}; }

    auto get_env() const noexcept {
        return std::execution::prop(std::execution::get_await_completion_adaptor,
            [](transformed&& sender) noexcept -> awaitable_tests::lvalue_sender& { return sender.member; });
    }
};

template<class T>
auto operator co_await(T&&) noexcept -> awaitable_tests::int_awaiter { return {}; }
} // namespace bridge_intermediate_adl

namespace bridge_prepared_adl {
struct sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) && -> awaitable_tests::lvalue_sender::operation<R> { return {std::move(r)}; }
};

template<class T>
auto operator co_await(T&&) noexcept -> awaitable_tests::int_awaiter { return {}; }
} // namespace bridge_prepared_adl

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
using a::owned_int;
using a::owned_void;
using a::owned_error;
using a::owned_suspend_error;
using a::owned_stopped;

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

TEST(AwaitableSender, CvVoidAwaitResumeCompletesWithNoValueArguments) {
    struct receiver {
        using receiver_concept = ex::receiver_tag;
        int* values;
        void set_value() && noexcept { ++*values; }
        void set_error(std::exception_ptr) && noexcept { std::terminate(); }
        void set_stopped() && noexcept { std::terminate(); }
    };
    int values = 0;
    auto op = ex::connect(a::cv_void_awaiter{}, receiver{&values});
    ex::start(op);
    EXPECT_EQ(values, 1);
}

TEST(AwaitableSender, SyncWaitTranslatesExceptionAndStopped) {
    bool destroyed = false;
    EXPECT_THROW(std::this_thread::sync_wait(owned_error{&destroyed}), std::runtime_error);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_THROW(std::this_thread::sync_wait(owned_suspend_error{&destroyed}), std::runtime_error);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_FALSE(std::this_thread::sync_wait(owned_stopped{&destroyed}));
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
    auto member = std::this_thread::sync_wait(a::member_awaitable{});
    auto free = std::this_thread::sync_wait(a::free_awaitable{});
    auto adapted = std::this_thread::sync_wait(a::member_adapted{});
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

TEST(AwaitableSender, LockedAlgorithmsComposeThroughUnchangedCpos) {
    auto combined = std::this_thread::sync_wait(ex::when_all(a::int_awaiter{}, ex::just(25)));
    ASSERT_TRUE(combined);
    EXPECT_EQ(*combined, std::make_tuple(17, 25));
    ex::counting_scope scope;
    auto wrapped = std::this_thread::sync_wait(scope.get_token().wrap(a::int_awaiter{}));
    ASSERT_TRUE(wrapped);
    EXPECT_EQ(std::get<0>(*wrapped), 17);
    auto standard = std::this_thread::sync_wait(a::int_awaiter{});
    ASSERT_TRUE(standard);
    EXPECT_EQ(std::get<0>(*standard), 17);
    auto associated = std::this_thread::sync_wait(ex::associate(a::int_awaiter{}, scope.get_token()));
    ASSERT_TRUE(associated);
    EXPECT_EQ(std::get<0>(*associated), 17);
    auto future = ex::spawn_future(a::int_awaiter{}, scope.get_token());
    auto result = std::this_thread::sync_wait(std::move(future));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 17);
    EXPECT_TRUE(std::this_thread::sync_wait(scope.join()));
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

struct bridge_counts {
    int copies = 0;
    int moves = 0;
    int lvalue_connects = 0;
    int rvalue_connects = 0;
};

struct category_sender {
    using sender_concept = ex::sender_tag;
    bridge_counts* counts;
    int value;

    category_sender(bridge_counts& counts, int value) noexcept : counts(&counts), value(value) {}
    category_sender(const category_sender& other) noexcept
        : counts(other.counts), value(other.value) { ++counts->copies; }
    category_sender(category_sender&& other) noexcept
        : counts(other.counts), value(std::exchange(other.value, -1)) { ++counts->moves; }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> std::conditional_t<
        std::is_lvalue_reference_v<Self>,
        ex::completion_signatures<ex::set_value_t(int)>,
        ex::completion_signatures<ex::set_value_t(long)>> { return {}; }

    template<class R, class T>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        T value;
        void start() & noexcept { ex::set_value(std::move(receiver), std::move(value)); }
    };

    template<ex::receiver R>
    auto connect(R r) & noexcept -> operation<R, int> {
        ++counts->lvalue_connects;
        return {std::move(r), value};
    }
    template<ex::receiver R>
    auto connect(R r) && noexcept -> operation<R, long> {
        ++counts->rvalue_connects;
        return {std::move(r), value};
    }
};

environment_task await_named_sender_twice(int* output) {
    auto source = ex::just(21);
    auto first = co_await source;
    auto second = co_await source;
    auto reused = std::this_thread::sync_wait(source);
    EXPECT_TRUE(reused.has_value());
    *output = first + second + std::get<0>(reused.value_or(std::tuple{0}));
}

TEST(AwaitableSender, NamedLibrarySenderCanBeAwaitedTwiceAndReused) {
    int result = 0;
    await_named_sender_twice(&result);
    EXPECT_EQ(result, 63);
}

environment_task await_by_category(int* output) {
    bridge_counts counts;
    category_sender source{counts, 29};
    auto borrowed = co_await source;
    static_assert(std::same_as<decltype(borrowed), int>);
    EXPECT_EQ(borrowed, 29);
    EXPECT_EQ(counts.lvalue_connects, 1);
    EXPECT_EQ(counts.rvalue_connects, 0);
    EXPECT_EQ(counts.copies, 0);
    EXPECT_EQ(counts.moves, 0);
    EXPECT_EQ(source.value, 29);

    auto owned = co_await std::move(source);
    static_assert(std::same_as<decltype(owned), long>);
    EXPECT_EQ(owned, 29L);
    EXPECT_EQ(counts.lvalue_connects, 1);
    EXPECT_EQ(counts.rvalue_connects, 1);
    EXPECT_EQ(counts.copies, 0);
    EXPECT_GT(counts.moves, 0);
    EXPECT_EQ(source.value, -1);
    *output = borrowed;
}

TEST(AwaitableSender, SenderCategoryControlsSignaturesAndConnectionWithoutCopying) {
    int result = 0;
    await_by_category(&result);
    EXPECT_EQ(result, 29);
}

environment_task await_adapted_lvalue(int* output) {
    bridge_counts counts;
    category_sender prepared{counts, 37};
    *output = co_await a::bridge_adaptor_source<category_sender>{&prepared};
    EXPECT_EQ(counts.lvalue_connects, 1);
    EXPECT_EQ(counts.rvalue_connects, 0);
    EXPECT_EQ(counts.copies, 0);
    EXPECT_EQ(counts.moves, 0);
}

TEST(AwaitableSender, CompletionAdaptorCanReturnAnExternalSenderByReference) {
    int result = 0;
    await_adapted_lvalue(&result);
    EXPECT_EQ(result, 37);
}

template<class Promise = environment_task::promise_type>
struct current_promise {
    Promise* promise = nullptr;
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        promise = std::addressof(h.promise());
        return false;
    }
    auto await_resume() const noexcept -> Promise& { return *promise; }
};

struct owner_lifetime {
    bool destroyed = false;
    bool alive_at_op_destruction = false;
    int adaptor_calls = 0;
    int value = 0;
    void* pending = nullptr;
    void (*complete)(void*) noexcept = nullptr;
};

struct owner_member_sender {
    using sender_concept = ex::sender_tag;
    owner_lifetime* lifetime;
    int value = 53;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        owner_lifetime* lifetime;
        const int* borrowed_value;
        R receiver;

        ~operation() { lifetime->alive_at_op_destruction = !lifetime->destroyed; }

        void start() & noexcept {
            lifetime->pending = this;
            lifetime->complete = [](void* pending) noexcept {
                auto& op = *static_cast<operation*>(pending);
                const bool destroyed = op.lifetime->destroyed;
                EXPECT_FALSE(destroyed);
                ex::set_value(std::move(op.receiver), destroyed ? -1 : *op.borrowed_value);
                // Completion may have destroyed op and the owner.
            };
        }
    };

    template<ex::receiver R>
    auto connect(R r) & noexcept -> operation<R> {
        return {lifetime, &value, std::move(r)};
    }
};

struct transformed_owner {
    using sender_concept = ex::sender_tag;
    owner_member_sender member;

    explicit transformed_owner(owner_lifetime* lifetime) noexcept : member{lifetime} {}
    transformed_owner(transformed_owner&& other) noexcept
        : member{std::exchange(other.member.lifetime, nullptr), other.member.value} {}
    ~transformed_owner() { if (member.lifetime) member.lifetime->destroyed = true; }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    auto get_env() const noexcept {
        return ex::prop(ex::get_await_completion_adaptor,
            [](transformed_owner&& self) noexcept -> owner_member_sender& {
                ++self.member.lifetime->adaptor_calls;
                return self.member;
            });
    }
};

struct owner_source {
    using sender_concept = ex::sender_tag;
    owner_lifetime* lifetime;

    struct domain {
        template<class Env>
        auto transform_sender(ex::set_value_t, owner_source&& source, const Env&) const noexcept
            -> transformed_owner { return transformed_owner{source.lifetime}; }
    };

    auto get_env() const noexcept { return ex::prop(ex::get_completion_domain<ex::set_value_t>, domain{}); }
};

environment_task await_transformed_member(owner_lifetime& lifetime) {
    auto& promise = co_await current_promise<>{};
    auto awaiter = ex::as_awaitable(owner_source{&lifetime}, promise);
    EXPECT_EQ(lifetime.adaptor_calls, 1);
    EXPECT_EQ(lifetime.pending, nullptr);
    EXPECT_FALSE(lifetime.destroyed);
    if (lifetime.destroyed) co_return; // Fail safely before connecting a dangling reference.
    lifetime.value = co_await awaiter;
    EXPECT_FALSE(lifetime.destroyed);
}

TEST(AwaitableSender, CompletionAdaptorCanBorrowMemberOfTransformedTemporary) {
    owner_lifetime lifetime;
    await_transformed_member(lifetime);
    EXPECT_FALSE(lifetime.destroyed);
    ASSERT_NE(lifetime.pending, nullptr);
    lifetime.complete(lifetime.pending);
    EXPECT_EQ(lifetime.value, 53);
    EXPECT_EQ(lifetime.adaptor_calls, 1);
    EXPECT_TRUE(lifetime.alive_at_op_destruction);
    EXPECT_TRUE(lifetime.destroyed);
}

enum chain_stage : std::size_t {
    promise_env_stage, transformed_stage, sender_env_stage, adaptor_stage,
    prepared_stage, operation_stage, stage_count
};
enum class chain_borrow { adaptor_member, env_member, promise_env, prepared_value };

struct chain_trace {
    std::array<int, stage_count> constructed{};
    std::array<int, stage_count> destroyed{};
    std::array<chain_stage, stage_count> teardown{};
    std::size_t teardown_size = 0;
    int prepared_moves = 0;
    int adaptor_calls = 0;
    int connect_calls = 0;
    int value = 0;
    bool throw_from_transform = false;
    bool throw_from_adaptor = false;
    void* pending = nullptr;
    void (*complete)(void*) noexcept = nullptr;

    bool owners_alive() const noexcept {
        for (std::size_t i = promise_env_stage; i <= adaptor_stage; ++i) {
            if (constructed[i] != 1 || destroyed[i] != 0) return false;
        }
        return true;
    }
};

struct chain_event {
    chain_trace* trace;
    chain_stage stage;

    chain_event(chain_trace& trace, chain_stage stage) noexcept : trace(&trace), stage(stage) {
        ++trace.constructed[stage];
    }
    chain_event(chain_event&& other) noexcept
        : trace(std::exchange(other.trace, nullptr)), stage(other.stage) {
        if (trace && stage == prepared_stage) ++trace->prepared_moves;
    }
    ~chain_event() {
        if (!trace) return;
        ++trace->destroyed[stage];
        if (trace->teardown_size < trace->teardown.size()) {
            trace->teardown[trace->teardown_size] = stage;
        }
        ++trace->teardown_size;
    }
};

struct chain_promise_env {
    chain_event event;
    int value = 61;

    explicit chain_promise_env(chain_trace& trace) noexcept : event(trace, promise_env_stage) {}
    chain_promise_env(chain_promise_env&&) = delete;
};

struct chain_sender {
    using sender_concept = ex::sender_tag;
    chain_trace* trace;
    const int* borrowed = nullptr;
    int member_value = 59;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        chain_event event;
        const int* borrowed;
        R receiver;

        void start() & noexcept {
            event.trace->pending = this;
            event.trace->complete = [](void* pending) noexcept {
                auto& op = *static_cast<operation*>(pending);
                auto& trace = *op.event.trace;
                const bool alive = trace.owners_alive();
                EXPECT_TRUE(alive);
                trace.pending = nullptr;
                ex::set_value(std::move(op.receiver), alive ? *op.borrowed : -1);
                // Operation and backing objects may be gone after completion.
            };
        }
    };

    template<ex::receiver R>
    auto connect(R r) const & noexcept -> operation<R> {
        ++trace->connect_calls;
        EXPECT_TRUE(trace->owners_alive());
        return {{*trace, operation_stage}, borrowed ? borrowed : &member_value, std::move(r)};
    }
};

struct chain_prepared_value : chain_sender {
    chain_event event;

    chain_prepared_value(chain_trace& trace, const int* value) noexcept
        : chain_sender{&trace, value}, event(trace, prepared_stage) {}
};

template<chain_borrow From>
struct chain_transformed;

template<chain_borrow From>
struct chain_adaptor {
    chain_event event;
    chain_sender member;
    chain_sender* env_member;
    int value = 67;

    chain_adaptor(chain_trace& trace, chain_sender* env_member) noexcept
        : event(trace, adaptor_stage), member{&trace}, env_member(env_member) {}
    chain_adaptor(chain_adaptor&&) = delete;

    decltype(auto) operator()(chain_transformed<From>&& sender) && {
        auto& trace = *event.trace;
        ++trace.adaptor_calls;
        if (trace.throw_from_adaptor) throw std::runtime_error("adaptation failed");
        if constexpr (From == chain_borrow::adaptor_member) {
            return (member);
        } else if constexpr (From == chain_borrow::env_member) {
            return *env_member;
        } else if constexpr (From == chain_borrow::promise_env) {
            return (sender.member);
        } else {
            return chain_prepared_value{trace, &value};
        }
    }
};

template<chain_borrow From>
struct chain_sender_env {
    chain_event event;
    mutable chain_sender member;

    explicit chain_sender_env(chain_trace& trace) noexcept
        : event(trace, sender_env_stage), member{&trace, nullptr, 63} {}
    chain_sender_env(chain_sender_env&&) = delete;

    auto query(ex::get_await_completion_adaptor_t) const noexcept -> chain_adaptor<From> {
        return {*event.trace, &member};
    }
};

template<chain_borrow From>
struct chain_transformed {
    using sender_concept = ex::sender_tag;
    chain_event event;
    chain_sender member;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    auto get_env() const noexcept -> chain_sender_env<From> {
        return chain_sender_env<From>{*event.trace};
    }
};

template<chain_borrow From>
struct chain_source {
    using sender_concept = ex::sender_tag;

    struct domain {
        auto transform_sender(ex::set_value_t, chain_source&&, const chain_promise_env& env) const
            -> chain_transformed<From> {
            if (env.event.trace->throw_from_transform) throw std::runtime_error("transformation failed");
            return {{*env.event.trace, transformed_stage}, {env.event.trace, &env.value}};
        }
    };

    auto get_env() const noexcept { return ex::prop(ex::get_completion_domain<ex::set_value_t>, domain{}); }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
};

struct chain_task {
    struct promise_type : ex::with_awaitable_senders<promise_type> {
        chain_trace* trace;

        explicit promise_type(chain_trace& trace) noexcept : trace(&trace) {}
        chain_task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        auto get_env() const noexcept -> chain_promise_env { return chain_promise_env{*trace}; }
    };
};

template<chain_borrow From>
chain_task await_chain(chain_trace& trace) {
    auto& promise = co_await current_promise<chain_task::promise_type>{};
    auto bridge = ex::as_awaitable(chain_source<From>{}, promise);
    EXPECT_EQ(trace.adaptor_calls, 1);
    EXPECT_EQ(trace.connect_calls, 0);
    EXPECT_TRUE(trace.owners_alive());
    if (!trace.owners_alive()) co_return; // Do not connect through a dead backing object.
    trace.value = co_await bridge;
    EXPECT_TRUE(trace.owners_alive());
}

template<chain_borrow From>
void check_chain_value(chain_trace& trace, int expected) {
    await_chain<From>(trace);
    ASSERT_NE(trace.pending, nullptr);
    trace.complete(trace.pending);
    EXPECT_EQ(trace.value, expected);
    EXPECT_EQ(trace.adaptor_calls, 1);
    EXPECT_EQ(trace.connect_calls, 1);
    EXPECT_EQ(trace.destroyed, trace.constructed);
}

TEST(AwaitableSender, CompletionAdaptorCanBorrowItsOwnSenderMember) {
    chain_trace trace;
    check_chain_value<chain_borrow::adaptor_member>(trace, 59);
}

TEST(AwaitableSender, CompletionAdaptorCanBorrowSenderEnvironmentMember) {
    chain_trace trace;
    check_chain_value<chain_borrow::env_member>(trace, 63);
}

TEST(AwaitableSender, TransformedSenderCanBorrowPromiseEnvironment) {
    chain_trace trace;
    check_chain_value<chain_borrow::promise_env>(trace, 61);
}

TEST(AwaitableSender, PreparedPrvalueCanBorrowAdaptorAndUnwindsInReverseOrder) {
    chain_trace trace;
    check_chain_value<chain_borrow::prepared_value>(trace, 67);
    EXPECT_EQ(trace.prepared_moves, 0);
    EXPECT_EQ(trace.teardown_size, stage_count);
    EXPECT_EQ(trace.teardown, (std::array{operation_stage, prepared_stage, adaptor_stage,
        sender_env_stage, transformed_stage, promise_env_stage}));
}

TEST(AwaitableSender, ThrowingAdaptorUnwindsConstructedIntermediatesOnce) {
    chain_trace trace;
    trace.throw_from_adaptor = true;
    chain_task::promise_type promise{trace};
    EXPECT_THROW((void)ex::as_awaitable(chain_source<chain_borrow::prepared_value>{}, promise),
                 std::runtime_error);
    EXPECT_EQ(trace.adaptor_calls, 1);
    EXPECT_EQ(trace.connect_calls, 0);
    EXPECT_EQ(trace.constructed, (std::array{1, 1, 1, 1, 0, 0}));
    EXPECT_EQ(trace.destroyed, trace.constructed);
    EXPECT_EQ(trace.teardown_size, 4u);
    EXPECT_EQ(trace.teardown, (std::array{adaptor_stage, sender_env_stage, transformed_stage,
        promise_env_stage, promise_env_stage, promise_env_stage}));
}

TEST(AwaitableSender, ThrowingTransformUnwindsPartiallyConstructedChain) {
    chain_trace trace;
    trace.throw_from_transform = true;
    chain_task::promise_type promise{trace};
    EXPECT_THROW((void)ex::as_awaitable(chain_source<chain_borrow::prepared_value>{}, promise),
                 std::runtime_error);
    EXPECT_EQ(trace.adaptor_calls, 0);
    EXPECT_EQ(trace.connect_calls, 0);
    EXPECT_EQ(trace.constructed, (std::array{1, 0, 0, 0, 0, 0}));
    EXPECT_EQ(trace.destroyed, trace.constructed);
    EXPECT_EQ(trace.teardown_size, 1u);
    EXPECT_EQ(trace.teardown[0], promise_env_stage);
}

struct intermediate_adl_source {
    using sender_concept = ex::sender_tag;

    struct domain {
        template<class Env>
        auto transform_sender(ex::set_value_t, intermediate_adl_source&&, const Env&) const noexcept
            -> bridge_intermediate_adl::transformed { return {}; }
    };

    auto get_env() const noexcept { return ex::prop(ex::get_completion_domain<ex::set_value_t>, domain{}); }
};

struct prepared_adl_source {
    using sender_concept = ex::sender_tag;
    int* adaptor_calls;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    auto get_env() const noexcept {
        return ex::prop(ex::get_await_completion_adaptor,
            [calls = adaptor_calls](auto&&) noexcept -> bridge_prepared_adl::sender {
                ++*calls;
                return {};
            });
    }
};

environment_task await_sender_bridge_with_adl(int* connected, int* intercepted, int* adaptor_calls) {
    *connected = co_await intermediate_adl_source{};
    *intercepted = co_await prepared_adl_source{adaptor_calls};
}

TEST(AwaitableSender, CoAwaitAdlUsesPreparedSenderNamespaceButNotIntermediateNamespace) {
    int connected = 0;
    int intercepted = 0;
    int adaptor_calls = 0;
    await_sender_bridge_with_adl(&connected, &intercepted, &adaptor_calls);
    EXPECT_EQ(connected, 29); // From the connected sender, not the competing awaiter.
    EXPECT_EQ(intercepted, 17); // From the prepared sender namespace's awaiter.
    EXPECT_EQ(adaptor_calls, 1); // Lookup ran after preparation, not on the source.
}

environment_task await_explicit_bridge(int* output) {
    bridge_counts counts;
    category_sender source{counts, 0};
    auto& promise = co_await current_promise<>{};
    auto awaiter = ex::as_awaitable(source, promise);
    EXPECT_EQ(counts.lvalue_connects, 0);
    EXPECT_EQ(counts.rvalue_connects, 0);
    source.value = 47;
    *output = co_await awaiter;
    EXPECT_EQ(counts.lvalue_connects, 1);
    EXPECT_EQ(counts.rvalue_connects, 0);
    EXPECT_EQ(counts.copies, 0);
    EXPECT_EQ(counts.moves, 0);
}

TEST(AwaitableSender, DirectAsAwaitableBorrowsUntilDeferredConnection) {
    int result = 0;
    await_explicit_bridge(&result);
    EXPECT_EQ(result, 47);
}

TEST(AwaitableSender, PreparedMemberNoexceptIsUsable) {
    a::promise promise;
    auto awaiter = ex::as_awaitable(a::adapted_sender<true, true>{}, promise);
    EXPECT_EQ(awaiter.await_resume(), 17);
}
} // namespace

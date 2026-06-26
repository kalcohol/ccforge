#include <gtest/gtest.h>
#include <execution>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include "../../forge/runtime/forge_operation_destroy.hpp"
#include "test_execution_manual_sender.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using forge_execution_test::manual_state;
using forge_execution_test::manual_sender;
using forge_execution_test::wait_until_completed;
using forge_execution_test::wait_until_started;
using forge_execution_test::wait_until_stop_requested;
using namespace std::chrono_literals;

struct scope_probe_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* completed = nullptr;

    void set_value() && noexcept {
        if (completed) *completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct self_destroying_join_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;
    bool* completed = nullptr;

    void set_value() && noexcept {
        if (completed) *completed = true;
        context->destroy();
    }

    template<class E>
    void set_error(E&&) && noexcept {
        context->destroy();
    }

    void set_stopped() && noexcept {
        context->destroy();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

bool wait_for_flag(
    const std::atomic<bool>& flag,
    std::chrono::milliseconds timeout = 500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::yield();
    }
    return flag.load(std::memory_order_acquire);
}

template<class Scope>
void expect_join_start_returns_while_nonempty() {
    Scope scope;
    auto token = scope.get_token();
    auto assoc = token.try_associate();
    ASSERT_TRUE(static_cast<bool>(assoc));

    bool completed = false;
    auto op = std::execution::connect(scope.join(), scope_probe_receiver{&completed});

    std::execution::start(op);

    EXPECT_FALSE(completed);
    EXPECT_EQ(scope.count(), 1u);

    assoc = decltype(assoc){};

    EXPECT_TRUE(completed);
    EXPECT_EQ(scope.count(), 0u);
}

template<class Scope>
void expect_multiple_joiners_complete_when_scope_drains() {
    Scope scope;
    auto token = scope.get_token();
    auto assoc = token.try_associate();
    ASSERT_TRUE(static_cast<bool>(assoc));

    bool first_completed = false;
    bool second_completed = false;
    auto first = std::execution::connect(
        scope.join(),
        scope_probe_receiver{&first_completed});
    auto second = std::execution::connect(
        scope.join(),
        scope_probe_receiver{&second_completed});

    std::execution::start(first);
    std::execution::start(second);

    EXPECT_FALSE(first_completed);
    EXPECT_FALSE(second_completed);

    assoc = decltype(assoc){};

    EXPECT_TRUE(first_completed);
    EXPECT_TRUE(second_completed);
    EXPECT_EQ(scope.count(), 0u);
}

template<class Scope>
void expect_join_receiver_may_destroy_operation_on_completion() {
    Scope scope;
    auto token = scope.get_token();
    auto assoc = token.try_associate();
    ASSERT_TRUE(static_cast<bool>(assoc));

    auto sender = scope.join();
    using sender_t = decltype(sender);
    using receiver_t = self_destroying_join_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t&&>(),
        std::declval<receiver_t>()));

    bool completed = false;
    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            std::move(sender),
            self_destroying_join_receiver{&context, &completed});
    });
    std::execution::start(op);

    EXPECT_FALSE(completed);
    EXPECT_FALSE(destroyed);

    assoc = decltype(assoc){};

    EXPECT_TRUE(completed);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}

struct pending_sender {
    using sender_concept = std::execution::sender_t;

    bool* started = nullptr;

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

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        bool* started;

        void start() & noexcept {
            if (started) *started = true;
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r), started};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> op<R> {
        return op<R>{std::move(r), started};
    }
};

struct throwing_connect_error {};

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

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        void start() & noexcept {}
    };

    template<std::execution::receiver R>
    auto connect(R) && -> op<R> {
        throw throwing_connect_error{};
    }
};

struct increment_sender {
    using sender_concept = std::execution::sender_t;

    std::atomic<int>* counter = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        std::atomic<int>* counter;

        void start() & noexcept {
            counter->fetch_add(1, std::memory_order_relaxed);
            std::execution::set_value(std::move(rcvr));
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r), counter};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> op<R> {
        return op<R>{std::move(r), counter};
    }
};

struct move_only_increment_sender {
    using sender_concept = std::execution::sender_t;

    std::unique_ptr<int> value;
    int* observed = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        std::unique_ptr<int> value;
        int* observed;

        void start() & noexcept {
            *observed = *value;
            std::execution::set_value(std::move(rcvr));
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r), std::move(value), observed};
    }
};

struct manual_void_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<manual_state> state;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        struct stop_callback {
            op* self;

            void operator()() noexcept {
                self->complete_stopped();
            }
        };

        using env_t = std::execution::env_of_t<R>;
        using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));
        using callback_t = std::stop_callback_for_t<token_t, stop_callback>;

        R rcvr;
        std::shared_ptr<manual_state> state;
        std::optional<callback_t> callback;
        std::atomic<bool> done{false};

        void start() & noexcept {
            auto token = std::execution::get_stop_token(std::execution::get_env(rcvr));
            {
                std::lock_guard lk{state->mtx};
                state->started = true;
                state->complete_value = [this](int) noexcept {
                    complete_value();
                };
            }
            state->cv.notify_all();

            if (token.stop_possible()) {
                callback.emplace(token, stop_callback{this});
            }
        }

        void complete_value() noexcept {
            if (done.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            {
                std::lock_guard lk{state->mtx};
                state->completed = true;
                state->complete_value = {};
            }
            state->cv.notify_all();
            callback.reset();
            std::execution::set_value(std::move(rcvr));
        }

        void complete_stopped() noexcept {
            if (done.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            {
                std::lock_guard lk{state->mtx};
                state->stop_requested = true;
                state->completed = true;
                state->complete_value = {};
            }
            state->cv.notify_all();
            std::execution::set_stopped(std::move(rcvr));
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r), std::move(state)};
    }
};

} // namespace

// counting_scope tests

TEST(SimpleCountingScopeTest, AssociationLifecycle) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    static_assert(std::execution::scope_association<association_t>);
    static_assert(std::execution::scope_token<decltype(token)>);
    static_assert(std::execution::sender<decltype(scope.join())>);

    association_t empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    EXPECT_FALSE(static_cast<bool>(empty.try_associate()));
    EXPECT_EQ(scope.count(), 0u);

    {
        auto assoc = token.try_associate();
        EXPECT_TRUE(static_cast<bool>(assoc));
        EXPECT_EQ(scope.count(), 1u);

        {
            auto nested = assoc.try_associate();
            EXPECT_TRUE(static_cast<bool>(nested));
            EXPECT_EQ(scope.count(), 2u);
        }

        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, JoinSenderCompletesWhenEmpty) {
    std::execution::simple_counting_scope scope;

    auto result = std::execution::sync_wait(scope.join());

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, JoinStartReturnsWhileScopeIsNonEmpty) {
    expect_join_start_returns_while_nonempty<std::execution::simple_counting_scope>();
}

TEST(SimpleCountingScopeTest, MultipleJoinersCompleteWhenScopeDrains) {
    expect_multiple_joiners_complete_when_scope_drains<std::execution::simple_counting_scope>();
}

TEST(SimpleCountingScopeTest, JoinReceiverMayDestroyOperationOnCompletion) {
    expect_join_receiver_may_destroy_operation_on_completion<
        std::execution::simple_counting_scope>();
}

TEST(SimpleCountingScopeTest, JoinDoesNotDeadlockSingleThreadScheduler) {
    forge::static_thread_pool pool{1};
    auto scheduler = pool.get_scheduler();

    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());
    auto assoc = std::make_shared<std::optional<association_t>>(token.try_associate());
    ASSERT_TRUE(assoc->has_value());

    std::atomic<bool> join_start_returned{false};
    std::atomic<bool> release_ran{false};
    std::atomic<bool> join_completed{false};

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            forge::start_detached(
                scope.join()
                | std::execution::then([&] noexcept {
                    join_completed.store(true, std::memory_order_release);
                }));
            join_start_returned.store(true, std::memory_order_release);
        }));

    ASSERT_TRUE(wait_for_flag(join_start_returned));
    EXPECT_FALSE(join_completed.load(std::memory_order_acquire));

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&, assoc] noexcept {
            assoc->reset();
            release_ran.store(true, std::memory_order_release);
        }));

    EXPECT_TRUE(wait_for_flag(release_ran));
    EXPECT_TRUE(wait_for_flag(join_completed));
    EXPECT_EQ(scope.count(), 0u);

    pool.wait();
}

TEST(SimpleCountingScopeTest, AssociationMoveTransfersOwnership) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    {
        auto first = token.try_associate();
        association_t second = std::move(first);

        EXPECT_FALSE(static_cast<bool>(first));
        EXPECT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(scope.count(), 1u);

        auto third = token.try_associate();
        EXPECT_EQ(scope.count(), 2u);

        third = std::move(second);
        EXPECT_FALSE(static_cast<bool>(second));
        EXPECT_TRUE(static_cast<bool>(third));
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, ClosedScopeReturnsDisengagedAssociation) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    scope.close();
    auto assoc = token.try_associate();

    EXPECT_FALSE(static_cast<bool>(assoc));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, SpawnAndJoin) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    std::atomic<int> counter{0};
    std::execution::spawn(increment_sender{&counter}, token);

    // inline_scheduler runs synchronously, so counter is already 1
    EXPECT_EQ(counter.load(), 1);
    (void)std::execution::sync_wait(scope.join());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, SpawnNonCopyableLvaluePipeline) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    int observed = 0;
    auto sndr = move_only_increment_sender{std::make_unique<int>(43), &observed};

    std::execution::spawn(std::move(sndr), token);

    EXPECT_EQ(observed, 43);
    (void)std::execution::sync_wait(scope.join());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, ClosePreventsFurtherSpawns) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();
    EXPECT_TRUE(scope.is_closed());

    std::atomic<int> counter{0};
    std::execution::spawn(increment_sender{&counter}, token);
    // spawn should silently ignore (scope closed)
    EXPECT_EQ(counter.load(), 0);
    (void)std::execution::sync_wait(scope.join());
}

TEST(SimpleCountingScopeTest, AssociateCompletesAndDisassociates) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(
        std::execution::associate(std::execution::just(42), token));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapCompletesAndDisassociates) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateClosedScopeCompletesStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    auto result = std::execution::sync_wait(
        std::execution::associate(std::execution::just(42), token));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapClosedScopeStillReturnsInputSender) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateDisassociatesOnError) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    EXPECT_THROW((void)std::execution::sync_wait(
        std::execution::associate(std::execution::just_error(42), token)), int);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, SpawnDisassociatesOnStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    std::execution::spawn(std::execution::just_stopped(), token);

    (void)std::execution::sync_wait(scope.join());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateOwnsAssociationUntilOperationDestruction) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto associated = std::execution::associate(pending_sender{&started}, token);
        EXPECT_EQ(scope.count(), 1u);

        auto op = std::execution::connect(
            std::move(associated),
            scope_probe_receiver{&completed});

        EXPECT_EQ(scope.count(), 1u);
        EXPECT_FALSE(started);

        std::execution::start(op);

        EXPECT_TRUE(started);
        EXPECT_FALSE(completed);
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, UnstartedAssociatedOperationReleasesAssociationOnDestruction) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto associated = std::execution::associate(pending_sender{&started}, token);
        EXPECT_EQ(scope.count(), 1u);

        auto op = std::execution::connect(
            std::move(associated),
            scope_probe_receiver{&completed});
        (void)op;
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_FALSE(started);
    EXPECT_FALSE(completed);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, MultipleSpawns) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
        std::execution::spawn(increment_sender{&counter}, token);
    }
    // inline_scheduler is synchronous
    EXPECT_EQ(counter.load(), 5);
    (void)std::execution::sync_wait(scope.join());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, IsDistinctAndAssociatesWork) {
    static_assert(!std::is_same_v<
                  std::execution::counting_scope,
                  std::execution::simple_counting_scope>);

    std::execution::counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    static_assert(std::execution::scope_association<association_t>);
    static_assert(std::execution::scope_token<decltype(token)>);
    static_assert(std::execution::sender<decltype(scope.join())>);

    {
        auto assoc = token.try_associate();
        EXPECT_TRUE(static_cast<bool>(assoc));
        EXPECT_EQ(scope.count(), 1u);

        auto nested = assoc.try_associate();
        EXPECT_TRUE(static_cast<bool>(nested));
        EXPECT_EQ(scope.count(), 2u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, JoinStartReturnsWhileScopeIsNonEmpty) {
    expect_join_start_returns_while_nonempty<std::execution::counting_scope>();
}

TEST(CountingScopeTest, MultipleJoinersCompleteWhenScopeDrains) {
    expect_multiple_joiners_complete_when_scope_drains<std::execution::counting_scope>();
}

TEST(CountingScopeTest, JoinReceiverMayDestroyOperationOnCompletion) {
    expect_join_receiver_may_destroy_operation_on_completion<std::execution::counting_scope>();
}

TEST(CountingScopeTest, WrapPreservesCompletionResults) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    auto value = std::execution::sync_wait(token.wrap(std::execution::just(42)));
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(std::get<0>(*value), 42);
    EXPECT_EQ(scope.count(), 0u);

    EXPECT_THROW((void)std::execution::sync_wait(
        token.wrap(std::execution::just_error(42))), int);
    EXPECT_EQ(scope.count(), 0u);

    auto stopped = std::execution::sync_wait(token.wrap(std::execution::just_stopped()));
    EXPECT_FALSE(stopped.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, CloseRejectsNewAssociatedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    scope.close();
    auto result = std::execution::sync_wait(
        std::execution::associate(std::execution::just(42), token));

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(scope.is_closed());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapExposesStoppableToken) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(
        token.wrap(std::execution::read_env(std::execution::get_stop_token)));

    ASSERT_TRUE(result.has_value());
    auto stop_token = std::get<0>(*result);
    static_assert(std::stoppable_token<decltype(stop_token)>);
    EXPECT_TRUE(stop_token.stop_possible());
    EXPECT_FALSE(stop_token.stop_requested());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapFusesPrerequestedReceiverStopToken) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    std::inplace_stop_source downstream_stop;
    downstream_stop.request_stop();
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, downstream_stop.get_token()));

    auto result = std::execution::sync_wait(
        std::execution::write_env(
            token.wrap(std::execution::read_env(std::execution::get_stop_token)),
            env));

    ASSERT_TRUE(result.has_value());
    auto stop_token = std::get<0>(*result);
    static_assert(std::stoppable_token<decltype(stop_token)>);
    EXPECT_TRUE(stop_token.stop_possible());
    EXPECT_TRUE(stop_token.stop_requested());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapFusesReceiverStopAfterStart) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    std::inplace_stop_source downstream_stop;
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, downstream_stop.get_token()));
    auto state = std::make_shared<manual_state>();

    std::atomic<bool> sync_wait_returned{false};
    std::atomic<bool> sync_wait_stopped{false};
    std::thread waiter([&] {
        auto result = std::execution::sync_wait(
            std::execution::write_env(
                token.wrap(manual_sender{state}),
                env));
        sync_wait_stopped.store(!result.has_value(), std::memory_order_release);
        sync_wait_returned.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(wait_until_started(state));

    downstream_stop.request_stop();
    const bool completed_by_downstream = wait_until_completed(state);
    if (!completed_by_downstream) {
        scope.request_stop();
        EXPECT_TRUE(wait_until_completed(state));
    }

    waiter.join();

    EXPECT_TRUE(completed_by_downstream);
    EXPECT_TRUE(sync_wait_returned.load(std::memory_order_acquire));
    EXPECT_TRUE(sync_wait_stopped.load(std::memory_order_acquire));
    {
        std::lock_guard lk{state->mtx};
        EXPECT_EQ(state->stop_completion_attempts, 1);
    }
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapFusesScopeStopAfterStart) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    std::atomic<bool> sync_wait_returned{false};
    std::atomic<bool> sync_wait_stopped{false};
    std::thread waiter([&] {
        auto result = std::execution::sync_wait(
            token.wrap(manual_sender{state}));
        sync_wait_stopped.store(!result.has_value(), std::memory_order_release);
        sync_wait_returned.store(true, std::memory_order_release);
    });

    if (!wait_until_started(state)) {
        scope.request_stop();
        waiter.join();
        FAIL() << "wrapped sender did not start";
    }

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));

    waiter.join();

    EXPECT_TRUE(sync_wait_returned.load(std::memory_order_acquire));
    EXPECT_TRUE(sync_wait_stopped.load(std::memory_order_acquire));
    {
        std::lock_guard lk{state->mtx};
        EXPECT_EQ(state->stop_completion_attempts, 1);
    }
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapCompletesOnceWhenScopeAndReceiverStopRace) {
    constexpr int iterations = 64;
    for (int i = 0; i < iterations; ++i) {
        std::execution::counting_scope scope;
        auto token = scope.get_token();
        std::inplace_stop_source downstream_stop;
        auto env = std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, downstream_stop.get_token()));
        auto state = std::make_shared<manual_state>();
        std::atomic<bool> sync_wait_stopped{false};
        std::atomic<bool> go{false};

        std::thread waiter([&] {
            auto result = std::execution::sync_wait(
                std::execution::write_env(
                    token.wrap(manual_sender{state}),
                    env));
            sync_wait_stopped.store(!result.has_value(), std::memory_order_release);
        });

        if (!wait_until_started(state)) {
            scope.request_stop();
            downstream_stop.request_stop();
            waiter.join();
            FAIL() << "wrapped sender did not start";
        }

        std::thread scope_stop([&] {
            while (!go.load(std::memory_order_acquire)) {}
            scope.request_stop();
        });
        std::thread receiver_stop([&] {
            while (!go.load(std::memory_order_acquire)) {}
            downstream_stop.request_stop();
        });

        go.store(true, std::memory_order_release);
        scope_stop.join();
        receiver_stop.join();

        EXPECT_TRUE(wait_until_completed(state));
        waiter.join();

        EXPECT_TRUE(sync_wait_stopped.load(std::memory_order_acquire));
        {
            std::lock_guard lk{state->mtx};
            EXPECT_TRUE(state->stop_requested);
            EXPECT_EQ(state->stop_completion_attempts, 1);
        }
        EXPECT_EQ(scope.count(), 0u);
    }
}

TEST(CountingScopeTest, WrapConnectFailureCompletesErrorInsteadOfTerminating) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    EXPECT_THROW((void)std::execution::sync_wait(
        token.wrap(throwing_connect_sender{})), throwing_connect_error);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, RequestStopCancelsSpawnedWrappedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    std::execution::spawn(manual_void_sender{state}, token);

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));

    (void)std::execution::sync_wait(scope.join());
    EXPECT_EQ(scope.count(), 0u);
}

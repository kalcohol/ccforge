#include <gtest/gtest.h>
#include <execution>
#include "test_execution_manual_sender.hpp"
#include <atomic>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using forge_execution_test::manual_sender;
using forge_execution_test::manual_state;
using forge_execution_test::wait_until_completed;
using forge_execution_test::wait_until_started;
using forge_execution_test::wait_until_stop_requested;

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

struct pending_sender {
    using sender_concept = std::execution::sender_t;

    bool* started = nullptr;

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

} // namespace

// counting_scope tests

TEST(SimpleCountingScopeTest, AssociationLifecycle) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    static_assert(std::execution::scope_association<association_t>);
    static_assert(std::execution::scope_token<decltype(token)>);

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
    token.spawn(std::execution::just() | std::execution::then([&counter] {
        counter.fetch_add(1, std::memory_order_relaxed);
    }));

    // inline_scheduler runs synchronously, so counter is already 1
    EXPECT_EQ(counter.load(), 1);
    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, ClosePreventsFurtherSpawns) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();
    EXPECT_TRUE(scope.is_closed());

    std::atomic<int> counter{0};
    token.spawn(std::execution::just() | std::execution::then([&counter] {
        counter.fetch_add(1, std::memory_order_relaxed);
    }));
    // spawn should silently ignore (scope closed)
    EXPECT_EQ(counter.load(), 0);
    scope.join();
}

TEST(SimpleCountingScopeTest, AssociateCompletesAndDisassociates) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(token.associate(std::execution::just(42)));

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

    auto result = std::execution::sync_wait(token.associate(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapClosedScopeCompletesStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateDisassociatesOnError) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    EXPECT_THROW((void)std::execution::sync_wait(
        token.associate(std::execution::just_error(42))), int);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, SpawnDisassociatesOnErrorAndStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    token.spawn(std::execution::just_error(42));
    token.spawn(std::execution::just_stopped());

    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapAcquiresAtStartAndReleasesOnOperationDestruction) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto op = std::execution::connect(
            token.wrap(pending_sender{&started}),
            scope_probe_receiver{&completed});

        EXPECT_EQ(scope.count(), 0u);
        EXPECT_FALSE(started);

        std::execution::start(op);

        EXPECT_TRUE(started);
        EXPECT_FALSE(completed);
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, UnstartedWrappedOperationDoesNotAssociate) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto op = std::execution::connect(
            token.wrap(pending_sender{&started}),
            scope_probe_receiver{&completed});
        (void)op;
        EXPECT_EQ(scope.count(), 0u);
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
        token.spawn(std::execution::just() | std::execution::then([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    // inline_scheduler is synchronous
    EXPECT_EQ(counter.load(), 5);
    scope.join();
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

TEST(CountingScopeTest, CloseRejectsNewWrappedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    scope.close();
    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(scope.is_closed());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapInjectsScopeStopToken) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(
        token.wrap(std::execution::read_env(std::execution::get_stop_token)));

    ASSERT_TRUE(result.has_value());
    auto stop_token = std::get<0>(*result);
    EXPECT_TRUE(stop_token.stop_possible());
    EXPECT_FALSE(stop_token.stop_requested());

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(stop_token.stop_requested());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, RequestStopCancelsSpawnedWrappedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    token.spawn(manual_sender{state});

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));

    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

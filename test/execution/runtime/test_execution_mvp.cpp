#include <gtest/gtest.h>

#include <execution>

#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

// ── T-1: Compile-time concept probes ────────────────────────────────────

// Positive probes — built-in sender types must satisfy sender / sender_in.
static_assert(std::execution::sender<decltype(std::execution::just(42))>);
static_assert(std::execution::sender_in<decltype(std::execution::just(42))>);
static_assert(std::execution::sender<decltype(std::execution::just_error(std::make_exception_ptr(0)))>);
static_assert(std::execution::sender<decltype(std::execution::just_stopped())>);

// Negative probes — plain types must NOT satisfy execution concepts.
static_assert(!std::execution::sender<int>);
static_assert(!std::execution::receiver<int>);
static_assert(!std::execution::scheduler<int>);
static_assert(!std::execution::operation_state<int>);

// ── T-5: receiver concept requires nothrow-move and non-final ────────────
namespace {

struct throwing_move_receiver {
    using receiver_concept = std::execution::receiver_t;
    throwing_move_receiver() = default;
    throwing_move_receiver(throwing_move_receiver&&) noexcept(false) {}
    throwing_move_receiver& operator=(throwing_move_receiver&&) = default;
    friend auto tag_invoke(std::execution::get_env_t, const throwing_move_receiver&) noexcept
        -> std::execution::empty_env { return {}; }
};
static_assert(!std::execution::receiver<throwing_move_receiver>,
              "receiver with throwing move ctor must be rejected");

struct final_receiver final {
    using receiver_concept = std::execution::receiver_t;
    friend auto tag_invoke(std::execution::get_env_t, const final_receiver&) noexcept
        -> std::execution::empty_env { return {}; }
};
static_assert(!std::execution::receiver<final_receiver>,
              "final receiver must be rejected");

// ── T-6: operation_state_concept marker is required ──────────────────────

struct no_marker_opstate {
    no_marker_opstate() = default;
    no_marker_opstate(no_marker_opstate&&) = delete;
    friend void tag_invoke(std::execution::start_t, no_marker_opstate&) noexcept {}
};
static_assert(!std::execution::operation_state<no_marker_opstate>,
              "operation_state without concept marker must be rejected");

} // namespace

// Completion-signature probes for just(42).
namespace {

using just_int_cs_t = decltype(std::execution::get_completion_signatures(
    std::execution::just(42), std::execution::empty_env{}));

// just(42) should produce completion_signatures<set_value_t(int)>.
static_assert(std::is_same_v<just_int_cs_t,
    std::execution::completion_signatures<std::execution::set_value_t(int)>>);

// just_stopped() should produce completion_signatures<set_stopped_t()>.
using just_stopped_cs_t = decltype(std::execution::get_completion_signatures(
    std::execution::just_stopped(), std::execution::empty_env{}));
static_assert(std::is_same_v<just_stopped_cs_t,
    std::execution::completion_signatures<std::execution::set_stopped_t()>>);

template<class T>
concept optional_like = requires(T t) {
    static_cast<bool>(t);
    *t;
};

} // namespace

TEST(ExecutionMvpTest, JustSyncWaitSingleValue) {
    auto result = std::execution::sync_wait(std::execution::just(42));

    static_assert(optional_like<decltype(result)>);
    ASSERT_TRUE(static_cast<bool>(result));

    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, JustSyncWaitMultipleValues) {
    auto result = std::execution::sync_wait(std::execution::just(1, std::string("ok")));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_EQ(std::get<1>(*result), "ok");
}

TEST(ExecutionMvpTest, ThenTransformsValue) {
    auto sender = std::execution::then(std::execution::just(10), [](int v) { return v + 5; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 15);
}

TEST(ExecutionMvpTest, ThenWorksWithPipeOperator) {
    auto sender = std::execution::just(10) | std::execution::then([](int v) { return v + 7; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 17);
}

TEST(ExecutionMvpTest, ThenVoidReturnBecomesEmptyTuple) {
    int observed = 0;
    auto sender = std::execution::then(std::execution::just(1), [&](int v) { observed = v; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(observed, 1);

    using tuple_t = std::remove_cvref_t<decltype(*result)>;
    static_assert(std::tuple_size_v<tuple_t> == 0);
}

TEST(ExecutionMvpTest, ThenExceptionPropagatesViaSyncWait) {
    auto sender = std::execution::then(std::execution::just(), []() -> int {
        throw std::runtime_error("boom");
    });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sender)), std::runtime_error);
}

TEST(ExecutionMvpTest, ThenForwardsErrorAndDoesNotCallFn) {
    bool called = false;
    auto sender =
        std::execution::just_error(std::runtime_error("err")) |
        std::execution::then([&](int) {
            called = true;
            return 0;
        });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sender)), std::runtime_error);
    EXPECT_FALSE(called);
}

TEST(ExecutionMvpTest, ThenForwardsStoppedAndDoesNotCallFn) {
    bool called = false;
    auto sender =
        std::execution::just_stopped() |
        std::execution::then([&] {
            called = true;
            return 0;
        });

    auto result = std::execution::sync_wait(std::move(sender));
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_FALSE(called);
}

// ── T-3: then with multi-value input ────────────────────────────────────

TEST(ExecutionMvpTest, ThenMultiValueInput) {
    auto sender = std::execution::just(1, 2) |
                  std::execution::then([](int a, int b) { return a + b; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 3);
}

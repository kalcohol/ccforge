#include <gtest/gtest.h>

#include <forge/start_detached.hpp>
#include <execution>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

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
using just_int_envless_cs_t = decltype(std::execution::get_completion_signatures(
    std::execution::just(42)));
using just_int_alias_cs_t = std::execution::completion_signatures_of_t<
    decltype(std::execution::just(42))>;

// just(42) should produce completion_signatures<set_value_t(int)>.
static_assert(std::is_same_v<just_int_cs_t,
    std::execution::completion_signatures<std::execution::set_value_t(int)>>);
static_assert(std::is_same_v<just_int_envless_cs_t, just_int_cs_t>);
static_assert(std::is_same_v<just_int_alias_cs_t, just_int_cs_t>);

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

struct deref_unique {
    int operator()(std::unique_ptr<int> p) const noexcept { return *p; }
};

struct int_receiver {
    using receiver_concept = std::execution::receiver_t;

    friend void tag_invoke(std::execution::set_value_t, int_receiver&&, int) noexcept {}
    friend void tag_invoke(std::execution::set_error_t, int_receiver&&, std::exception_ptr) noexcept {}
    friend void tag_invoke(std::execution::set_stopped_t, int_receiver&&) noexcept {}
    friend auto tag_invoke(std::execution::get_env_t, const int_receiver&) noexcept
        -> std::execution::empty_env { return {}; }
};

template<class R>
struct sync_wait_multi_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    bool use_double = false;

    sync_wait_multi_value_op(R r, bool use_double)
        : rcvr(std::move(r)), use_double(use_double) {}
    sync_wait_multi_value_op(sync_wait_multi_value_op&&) = delete;
    sync_wait_multi_value_op(const sync_wait_multi_value_op&) = delete;

    void start() & noexcept {
        if (use_double) {
            std::execution::set_value(std::move(rcvr), 4.5);
        } else {
            std::execution::set_value(std::move(rcvr), 3);
        }
    }
};

struct sync_wait_multi_value_sender {
    using sender_concept = std::execution::sender_t;

    bool use_double = false;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) && -> sync_wait_multi_value_op<R> {
        return sync_wait_multi_value_op<R>{std::move(r), use_double};
    }
};

template<class R>
struct sync_wait_empty_or_int_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    bool use_empty = false;

    sync_wait_empty_or_int_op(R r, bool use_empty)
        : rcvr(std::move(r)), use_empty(use_empty) {}
    sync_wait_empty_or_int_op(sync_wait_empty_or_int_op&&) = delete;
    sync_wait_empty_or_int_op(const sync_wait_empty_or_int_op&) = delete;

    void start() & noexcept {
        if (use_empty) {
            std::execution::set_value(std::move(rcvr));
        } else {
            std::execution::set_value(std::move(rcvr), 8);
        }
    }
};

struct sync_wait_empty_or_int_sender {
    using sender_concept = std::execution::sender_t;

    bool use_empty = false;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) && -> sync_wait_empty_or_int_op<R> {
        return sync_wait_empty_or_int_op<R>{std::move(r), use_empty};
    }
};

template<class R>
struct sync_wait_duplicate_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;

    explicit sync_wait_duplicate_value_op(R r) : rcvr(std::move(r)) {}
    sync_wait_duplicate_value_op(sync_wait_duplicate_value_op&&) = delete;
    sync_wait_duplicate_value_op(const sync_wait_duplicate_value_op&) = delete;

    void start() & noexcept {
        std::execution::set_value(std::move(rcvr), 11);
    }
};

struct sync_wait_duplicate_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) && -> sync_wait_duplicate_value_op<R> {
        return sync_wait_duplicate_value_op<R>{std::move(r)};
    }
};

struct sync_wait_throwing_value {
    sync_wait_throwing_value() = default;
    sync_wait_throwing_value(const sync_wait_throwing_value&) = default;
    sync_wait_throwing_value(sync_wait_throwing_value&&) {
        throw std::runtime_error("sync_wait value construction");
    }
};

template<class R>
struct sync_wait_throwing_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;

    explicit sync_wait_throwing_value_op(R r) : rcvr(std::move(r)) {}
    sync_wait_throwing_value_op(sync_wait_throwing_value_op&&) = delete;
    sync_wait_throwing_value_op(const sync_wait_throwing_value_op&) = delete;

    void start() & noexcept {
        std::execution::set_value(std::move(rcvr), sync_wait_throwing_value{});
    }
};

struct sync_wait_throwing_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(sync_wait_throwing_value)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) && -> sync_wait_throwing_value_op<R> {
        return sync_wait_throwing_value_op<R>{std::move(r)};
    }
};

using move_only_pipeline_t = decltype(
    std::execution::just(std::unique_ptr<int>{}) | std::execution::then(deref_unique{}));

template<class S>
concept rvalue_connectable_to_int_receiver = requires(S&& s, int_receiver r) {
    std::execution::connect(std::move(s), std::move(r));
};

template<class S>
concept lvalue_connectable_to_int_receiver = requires(S& s, int_receiver r) {
    std::execution::connect(s, std::move(r));
};

static_assert(!std::copy_constructible<move_only_pipeline_t>);
static_assert(rvalue_connectable_to_int_receiver<move_only_pipeline_t>);
static_assert(!lvalue_connectable_to_int_receiver<move_only_pipeline_t>);

} // namespace

TEST(ExecutionMvpTest, JustSyncWaitSingleValue) {
    auto result = std::execution::sync_wait(std::execution::just(42));

    static_assert(optional_like<decltype(result)>);
    ASSERT_TRUE(static_cast<bool>(result));

    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineSyncWaitRequiresMove) {
    auto sndr = std::execution::just(std::make_unique<int>(42))
        | std::execution::then(deref_unique{});

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineStartDetachedRequiresMove) {
    bool ran = false;
    auto sndr = std::execution::just(std::make_unique<int>(7))
        | std::execution::then([&](std::unique_ptr<int> value) noexcept {
              ran = (*value == 7);
          });

    forge::start_detached(std::move(sndr));

    EXPECT_TRUE(ran);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineStartsOnRequiresMove) {
    std::execution::inline_scheduler scheduler;
    auto source = std::execution::just(std::make_unique<int>(9))
        | std::execution::then(deref_unique{});
    auto sndr = std::execution::starts_on(scheduler, std::move(source));

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 9);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineContinuesOnRequiresMove) {
    std::execution::inline_scheduler scheduler;
    auto source = std::execution::just(std::make_unique<int>(11))
        | std::execution::then(deref_unique{});
    auto sndr = std::execution::continues_on(std::move(source), scheduler);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 11);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineSplitRequiresMove) {
    auto source = std::execution::just(std::make_unique<int>(13))
        | std::execution::then(deref_unique{});
    auto sndr = std::execution::split(std::move(source));

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineWhenAllRequiresMove) {
    auto first = std::execution::just(std::make_unique<int>(17))
        | std::execution::then(deref_unique{});
    auto second = std::execution::just(std::make_unique<int>(19))
        | std::execution::then(deref_unique{});
    auto sndr = std::execution::when_all(std::move(first), std::move(second));

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 17);
    EXPECT_EQ(std::get<1>(*result), 19);
}

TEST(ExecutionMvpTest, JustSyncWaitMultipleValues) {
    auto result = std::execution::sync_wait(std::execution::just(1, std::string("ok")));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_EQ(std::get<1>(*result), "ok");
}

TEST(ExecutionMvpTest, CopyableLvalueSenderConnectsByCopy) {
    auto sender = std::execution::just(42);

    auto first = std::execution::sync_wait(sender);
    auto second = std::execution::sync_wait(sender);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*first), 42);
    EXPECT_EQ(std::get<0>(*second), 42);
}

TEST(ExecutionMvpTest, MoveOnlySenderConnectsAsRvalue) {
    auto sender = std::execution::just(std::make_unique<int>(42))
                | std::execution::then(deref_unique{});

    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, TransferJustCompletesOnScheduler) {
    std::execution::inline_scheduler scheduler;

    auto result = std::execution::sync_wait(
        std::execution::transfer_just(scheduler, 42));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, ThenTransformsValue) {
    auto sender = std::execution::then(std::execution::just(10), [](int v) { return v + 5; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 15);
}

TEST(ExecutionMvpTest, SyncWaitDecaysReferenceValueSignatures) {
    int value = 42;
    auto sender = std::execution::just(&value)
                | std::execution::then([](int* p) -> int& { return *p; });

    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    using tuple_t = std::remove_cvref_t<decltype(*result)>;
    static_assert(std::is_same_v<tuple_t, std::tuple<int>>);
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, SyncWaitMultiValueAlternativesReturnVariant) {
    auto int_result = std::execution::sync_wait(sync_wait_multi_value_sender{false});
    using expected_t = std::optional<std::variant<std::tuple<int>, std::tuple<double>>>;
    static_assert(std::is_same_v<decltype(int_result), expected_t>);

    ASSERT_TRUE(int_result.has_value());
    ASSERT_TRUE(std::holds_alternative<std::tuple<int>>(*int_result));
    EXPECT_EQ(std::get<0>(std::get<std::tuple<int>>(*int_result)), 3);

    auto double_result = std::execution::sync_wait(sync_wait_multi_value_sender{true});
    ASSERT_TRUE(double_result.has_value());
    ASSERT_TRUE(std::holds_alternative<std::tuple<double>>(*double_result));
    EXPECT_EQ(std::get<0>(std::get<std::tuple<double>>(*double_result)), 4.5);
}

TEST(ExecutionMvpTest, SyncWaitEmptyValueAlternativeUsesEmptyTupleVariant) {
    auto empty_result = std::execution::sync_wait(sync_wait_empty_or_int_sender{true});
    using expected_t = std::optional<std::variant<std::tuple<>, std::tuple<int>>>;
    static_assert(std::is_same_v<decltype(empty_result), expected_t>);

    ASSERT_TRUE(empty_result.has_value());
    EXPECT_TRUE(std::holds_alternative<std::tuple<>>(*empty_result));

    auto int_result = std::execution::sync_wait(sync_wait_empty_or_int_sender{false});
    ASSERT_TRUE(int_result.has_value());
    ASSERT_TRUE(std::holds_alternative<std::tuple<int>>(*int_result));
    EXPECT_EQ(std::get<0>(std::get<std::tuple<int>>(*int_result)), 8);
}

TEST(ExecutionMvpTest, SyncWaitDuplicateValueAlternativesDeduplicate) {
    auto result = std::execution::sync_wait(sync_wait_duplicate_value_sender{});
    using expected_t = std::optional<std::tuple<int>>;
    static_assert(std::is_same_v<decltype(result), expected_t>);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 11);
}

TEST(ExecutionMvpTest, SyncWaitValueConstructionFailurePropagates) {
    EXPECT_THROW((void)std::execution::sync_wait(sync_wait_throwing_value_sender{}),
                 std::runtime_error);
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

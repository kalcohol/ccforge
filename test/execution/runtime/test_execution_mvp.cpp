#include <gtest/gtest.h>

#include <forge/start_detached.hpp>
#include <execution>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
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
static_assert(std::is_object_v<decltype(std::execution::just)>);
static_assert(std::is_object_v<decltype(std::execution::just_error)>);
static_assert(std::is_object_v<decltype(std::execution::just_stopped)>);
static_assert(std::is_object_v<decltype(std::execution::read_env)>);
static_assert(std::is_object_v<decltype(std::execution::starts_on)>);
static_assert(std::is_object_v<decltype(std::execution::continues_on)>);
static_assert(std::is_object_v<decltype(std::execution::when_all)>);
static_assert(std::is_object_v<decltype(std::execution::when_all_with_variant)>);
static_assert(std::is_object_v<decltype(std::execution::spawn_future)>);
static_assert(std::is_object_v<decltype(std::execution::as_awaitable)>);
static_assert(std::is_object_v<decltype(std::this_thread::sync_wait)>);
static_assert(std::is_object_v<decltype(std::this_thread::sync_wait_with_variant)>);

// Negative probes — plain types must NOT satisfy execution concepts.
static_assert(!std::execution::sender<int>);
static_assert(!std::execution::receiver<int>);
static_assert(!std::execution::scheduler<int>);
static_assert(!std::execution::operation_state<int>);

static_assert(std::same_as<
    std::execution::sender_t,
    std::execution::sender_tag>);
static_assert(std::same_as<
    std::execution::receiver_t,
    std::execution::receiver_tag>);
static_assert(std::same_as<
    std::execution::operation_state_t,
    std::execution::operation_state_tag>);
static_assert(std::same_as<
    std::execution::scheduler_t,
    std::execution::scheduler_tag>);

// ── T-5: receiver concept requires nothrow move but permits final types ──
namespace {

struct probe_sender_tag {};

struct tag_probe_sender {
    using sender_concept = std::execution::sender_tag;

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::size_t I>
        requires (I == 0)
    auto get() && noexcept -> probe_sender_tag {
        return {};
    }
};

static_assert(std::execution::sender<tag_probe_sender>);
static_assert(std::same_as<
    std::execution::tag_of_t<tag_probe_sender>,
    probe_sender_tag>);

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
static_assert(std::execution::receiver<final_receiver>,
              "final receiver must be accepted");

// ── T-6: operation_state_concept marker is required ──────────────────────

struct no_marker_opstate {
    no_marker_opstate() = default;
    no_marker_opstate(no_marker_opstate&&) = delete;
    friend void tag_invoke(std::execution::start_t, no_marker_opstate&) noexcept {}
};
static_assert(!std::execution::operation_state<no_marker_opstate>,
              "operation_state without concept marker must be rejected");

struct movable_opstate {
    using operation_state_concept = std::execution::operation_state_t;

    movable_opstate() = default;
    movable_opstate(movable_opstate&&) = default;

    void start() & noexcept {}
};
static_assert(std::execution::operation_state<movable_opstate>,
              "operation_state permits movable types");

struct potentially_throwing_destructor_opstate {
    using operation_state_concept = std::execution::operation_state_t;

    ~potentially_throwing_destructor_opstate() noexcept(false) {}

    void start() & noexcept {}
};
static_assert(std::execution::operation_state<potentially_throwing_destructor_opstate>,
              "operation_state does not constrain destruction beyond the core language");

} // namespace

// Completion-signature probes for just(42).
namespace {

using just_int_cs_t = decltype(std::execution::get_completion_signatures(
    std::execution::just(42), std::execution::empty_env{}));
using just_int_envless_cs_t = decltype(std::execution::get_completion_signatures(
    std::execution::just(42)));
using just_int_alias_cs_t = std::execution::completion_signatures_of_t<
    decltype(std::execution::just(42))>;
using just_int_value_types_t = std::execution::value_types_of_t<
    decltype(std::execution::just(42))>;
using just_error_types_t = std::execution::error_types_of_t<
    decltype(std::execution::just_error(std::string{"error"}))>;

struct reference_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(
        std::execution::get_env_t,
        const reference_value_sender&) noexcept -> std::execution::empty_env {
        return {};
    }

    friend auto tag_invoke(
        std::execution::get_completion_signatures_t,
        reference_value_sender,
        auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(const std::string&)> {
        return {};
    }
};

template<class... Ts>
struct type_pack {};

struct mixed_completion_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_stopped_t(),
            std::execution::set_value_t(int),
            std::execution::set_error_t(const long&),
            std::execution::set_value_t(double),
            std::execution::set_error_t(short),
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct rvalue_only_sender {
    using sender_concept = std::execution::sender_t;

    explicit rvalue_only_sender(int value) noexcept
        : value(value) {}

    rvalue_only_sender(rvalue_only_sender&&) noexcept = default;
    rvalue_only_sender(const rvalue_only_sender&) = delete;

    template<class Self, class Env>
        requires (!std::is_lvalue_reference_v<Self>)
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct operation {
        using operation_state_concept = std::execution::operation_state_t;

        R receiver;
        int value;

        operation(R receiver, int value) noexcept
            : receiver(std::move(receiver))
            , value(value) {}

        operation(operation&&) = delete;
        operation(const operation&) = delete;

        void start() & noexcept {
            std::execution::set_value(std::move(receiver), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R receiver) && noexcept -> operation<R> {
        return operation<R>{std::move(receiver), value};
    }

    int value;
};

struct increment_value {
    auto operator()(int value) const noexcept -> int {
        return value + 1;
    }
};

struct recover_error_value {
    auto operator()(std::exception_ptr) const noexcept -> int {
        return 0;
    }
};

struct recover_stopped_value {
    auto operator()() const noexcept -> int {
        return 0;
    }
};

struct bind_next_sender {
    auto operator()(int value) const {
        return std::execution::just(value + 1);
    }
};

struct observe_bulk_value {
    void operator()(int, int&) const noexcept {}
};

struct stopped_only_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct move_only_stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    move_only_stopped_receiver() = default;
    move_only_stopped_receiver(move_only_stopped_receiver&&) noexcept = default;
    move_only_stopped_receiver(const move_only_stopped_receiver&) = delete;

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

using reference_value_types_t =
    std::execution::value_types_of_t<reference_value_sender>;
using mixed_value_types_t =
    std::execution::value_types_of_t<mixed_completion_sender>;
using mixed_custom_value_types_t = std::execution::value_types_of_t<
    mixed_completion_sender,
    std::execution::empty_env,
    std::tuple,
    type_pack>;
using mixed_error_types_t =
    std::execution::error_types_of_t<mixed_completion_sender>;
using mixed_custom_error_types_t = std::execution::error_types_of_t<
    mixed_completion_sender,
    std::execution::empty_env,
    type_pack>;
using stopped_value_types_t =
    std::execution::value_types_of_t<decltype(std::execution::just_stopped())>;
using stopped_custom_value_types_t = std::execution::value_types_of_t<
    decltype(std::execution::just_stopped()),
    std::execution::empty_env,
    std::tuple,
    type_pack>;
using adapted_rvalue_only_sender_t = decltype(std::execution::then(
    rvalue_only_sender{41},
    increment_value{}));
using upon_error_rvalue_only_sender_t = decltype(std::execution::upon_error(
    rvalue_only_sender{1},
    recover_error_value{}));
using upon_stopped_rvalue_only_sender_t = decltype(std::execution::upon_stopped(
    rvalue_only_sender{1},
    recover_stopped_value{}));
using let_rvalue_only_sender_t = decltype(std::execution::let_value(
    rvalue_only_sender{1},
    bind_next_sender{}));
using bulk_rvalue_only_sender_t = decltype(std::execution::bulk(
    rvalue_only_sender{1},
    1,
    observe_bulk_value{}));
using write_env_rvalue_only_sender_t = decltype(std::execution::write_env(
    rvalue_only_sender{1},
    std::execution::empty_env{}));
using variant_rvalue_only_sender_t = decltype(std::execution::into_variant(
    rvalue_only_sender{1}));
using continues_on_rvalue_only_sender_t = decltype(std::execution::continues_on(
    rvalue_only_sender{1},
    std::execution::inline_scheduler{}));
using starts_on_rvalue_only_sender_t = decltype(std::execution::starts_on(
    std::execution::inline_scheduler{},
    rvalue_only_sender{1}));
using optional_rvalue_only_sender_t = decltype(
    std::execution::stopped_as_optional(rvalue_only_sender{1}));
using stopped_error_rvalue_only_sender_t = decltype(
    std::execution::stopped_as_error(
        rvalue_only_sender{1},
        std::exception_ptr{}));
using scoped_rvalue_only_sender_t = decltype(
    std::declval<std::execution::counting_scope::scope_token>().wrap(
        rvalue_only_sender{1}));
static_assert(std::same_as<
    std::execution::simple_counting_scope::token,
    std::execution::simple_counting_scope::scope_token>);
static_assert(std::same_as<
    std::execution::counting_scope::token,
    std::execution::counting_scope::scope_token>);
static_assert(std::same_as<
    decltype(std::declval<std::execution::simple_counting_scope&>().get_token()),
    std::execution::simple_counting_scope::token>);
static_assert(std::same_as<
    decltype(std::declval<std::execution::counting_scope&>().get_token()),
    std::execution::counting_scope::token>);

// just(42) should produce completion_signatures<set_value_t(int)>.
static_assert(std::is_same_v<just_int_cs_t,
    std::execution::completion_signatures<std::execution::set_value_t(int)>>);
static_assert(std::is_same_v<just_int_envless_cs_t, just_int_cs_t>);
static_assert(std::is_same_v<just_int_alias_cs_t, just_int_cs_t>);
static_assert(std::is_same_v<
    just_int_value_types_t,
    std::variant<std::tuple<int>>>);
static_assert(std::is_same_v<
    just_error_types_t,
    std::variant<std::string>>);
static_assert(std::is_same_v<
    reference_value_types_t,
    std::variant<std::tuple<std::string>>>);
static_assert(std::is_same_v<
    mixed_value_types_t,
    std::variant<std::tuple<int>, std::tuple<double>>>);
static_assert(std::is_same_v<
    mixed_custom_value_types_t,
    type_pack<std::tuple<int>, std::tuple<double>, std::tuple<int>>>);
static_assert(std::is_same_v<
    mixed_error_types_t,
    std::variant<long, short>>);
static_assert(std::is_same_v<
    mixed_custom_error_types_t,
    type_pack<const long&, short>>);
static_assert(!std::is_default_constructible_v<stopped_value_types_t>);
static_assert(std::is_same_v<
    stopped_custom_value_types_t,
    type_pack<>>);
static_assert(std::execution::sender<rvalue_only_sender>);
static_assert(!std::execution::sender<rvalue_only_sender&>);
static_assert(std::execution::sender_in<rvalue_only_sender>);
static_assert(!std::execution::sender_in<rvalue_only_sender&>);
static_assert(std::execution::sender_in<adapted_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<upon_error_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<upon_stopped_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<let_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<bulk_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<write_env_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<variant_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<continues_on_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<starts_on_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<optional_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<stopped_error_rvalue_only_sender_t>);
static_assert(std::execution::sender_in<scoped_rvalue_only_sender_t>);
static_assert(std::execution::receiver<move_only_stopped_receiver>);
static_assert(!std::execution::receiver<move_only_stopped_receiver&>);
static_assert(std::execution::sender_to<
    decltype(std::execution::just_stopped()),
    stopped_only_receiver>);
static_assert(!std::execution::sender_to<
    decltype(std::execution::just(1)),
    stopped_only_receiver>);
static_assert(!std::execution::sender_to<
    decltype(std::execution::just(1)),
    int>);
static_assert(!std::execution::sends_stopped<decltype(std::execution::just(42))>);
static_assert(std::execution::sends_stopped<
    decltype(std::execution::just_stopped())>);

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

TEST(ExecutionMvpTest, SyncWaitMapsErrorCodeToSystemError) {
    const auto code = std::make_error_code(std::errc::permission_denied);

    try {
        (void)std::execution::sync_wait(std::execution::just_error(code));
        FAIL() << "sync_wait did not throw";
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code(), code);
    }
}

TEST(ExecutionMvpTest, ThenWorksWithPipeOperator) {
    auto sender = std::execution::just(10) | std::execution::then([](int v) { return v + 7; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 17);
}

TEST(ExecutionMvpTest, ThenAcceptsRvalueOnlySourceSignatures) {
    auto sender = std::execution::then(
        rvalue_only_sender{41},
        increment_value{});
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
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

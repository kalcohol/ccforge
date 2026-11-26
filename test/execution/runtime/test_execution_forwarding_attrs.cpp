#include "test_bulk.hpp"
#include <execution>
#include <gtest/gtest.h>

#include "../bulk_policy_probe.hpp"
#include "../forwarding_attrs_probe.hpp"

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace forwarding_attrs_probe;

struct result_state {
    int values = 0;
    int errors = 0;
    int stopped = 0;
};

struct receiver {
    using receiver_concept = ex::receiver_tag;
    result_state* result;

    template<class... Args>
    void set_value(Args&&...) && noexcept { ++result->values; }
    template<class Error>
    void set_error(Error&&) && noexcept { ++result->errors; }
    void set_stopped() && noexcept { ++result->stopped; }
    auto get_env() const noexcept -> receiver_env { return {}; }
};

template<class Sender>
void check_query_values(const Sender& sender, attribute_state& state) {
    state.reads = 0;
    const int copies = state.copies;
    const int moves = state.moves;
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(state.reads, 1);
    EXPECT_EQ(state.copies, copies);
    EXPECT_EQ(state.moves, moves);
    EXPECT_EQ(attrs.query(value_query{}), 17);
    EXPECT_EQ(value_query{}(attrs), 17);
    EXPECT_EQ(attrs.query(marked_query{}), 17);
    EXPECT_EQ(marked_query{}(attrs), 17);
    EXPECT_EQ(attrs.query(reference_query{}), 17);
    EXPECT_EQ(std::addressof(attrs.query(reference_query{})),
              std::addressof(reference_query{}(attrs)));

    argument arg;
    EXPECT_EQ(attrs.query(argument_query{}, arg, 5), 25);
    EXPECT_EQ(argument_query{}(attrs, arg, 5), 25);
    EXPECT_EQ(attrs.query(argument_query{}, std::move(arg), 5), 125);
    EXPECT_EQ(argument_query{}(attrs, argument{}, 5), 125);

    auto domain = ex::get_completion_domain<>(attrs, receiver_env{});
    static_assert(std::is_same_v<decltype(domain), member_domain>);
    auto value_domain = ex::get_completion_domain<ex::set_value_t>(attrs, receiver_env{});
    static_assert(std::is_same_v<decltype(value_domain), tag_domain>);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

template<class Sender>
void check_value_completion(Sender sender, attribute_state& state) {
    result_state result;
    auto op = ex::connect(std::move(sender), receiver{&result});
    ex::start(op);
    EXPECT_EQ(result.values, 1);
    EXPECT_EQ(result.errors, 0);
    EXPECT_EQ(result.stopped, 0);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

} // namespace

TEST(ExecutionForwardingAttrs, OwnedValuesUseMemberFirstAndRetainLegacyTagQueries) {
    attribute_state state;
    for_each_adaptor(source<>{&state}, [&](auto sender) {
        check_query_values(sender, state);
        EXPECT_EQ(state.alive, 0);
        check_value_completion(std::move(sender), state);
        EXPECT_EQ(state.alive, 0);
    });
}

TEST(ExecutionForwardingAttrs, ImmovablePrvaluesAreOwnedWithoutCopiesOrMoves) {
    attribute_state state;
    for_each_adaptor(source<immovable_attributes>{&state}, [&](auto sender) {
        {
            auto attrs = ex::get_env(sender);
            EXPECT_EQ(state.alive, 1);
            EXPECT_EQ(value_query{}(attrs), 17);
        }
        EXPECT_EQ(state.alive, 0);
        check_query_values(sender, state);
        check_value_completion(std::move(sender), state);
        EXPECT_EQ(state.alive, 0);
    });
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);
}

TEST(ExecutionForwardingAttrs, BorrowedNoncopyableAttributesRemainBorrowedAcrossCopies) {
    attribute_state state;
    immovable_attributes borrowed(state);
    EXPECT_EQ(actual_nonforwarding(borrowed), 17);
    auto verify = [&](auto sender) {
        check_query_values(sender, state);
        auto attrs = ex::get_env(sender);
        auto attrs_copy = attrs;
        EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
        EXPECT_EQ(std::addressof(reference_query{}(attrs_copy)), std::addressof(borrowed.value));
        EXPECT_EQ(state.alive, 1);
        check_value_completion(std::move(sender), state);
    };
    for_each_adaptor(source<immovable_attributes, true>{&state, &borrowed}, verify);
    for_each_adaptor(source<immovable_attributes, true, false>{&state, &borrowed}, verify);
    EXPECT_EQ(state.alive, 1);
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);
}

TEST(ExecutionForwardingAttrs, ActualQueriesOutsideDefaultGateRemainRawValid) {
    actual_query<true>::default_constructions = 0;
    actual_query<false>::default_constructions = 0;
    actual_query<true> runtime_query;
    EXPECT_TRUE(std::forwarding_query(runtime_query));
    EXPECT_EQ(actual_query<true>::default_constructions, 1);
    actual_query<true>::default_constructions = 0;

    attribute_state state;
    immovable_attributes borrowed(state);
    EXPECT_EQ(actual_forwarding(borrowed), 17);
    EXPECT_EQ(actual_nonforwarding(borrowed), 17);
    EXPECT_EQ(std::addressof(actual_forwarding(borrowed)), std::addressof(borrowed.value));
    argument arg;
    EXPECT_EQ(std::addressof(actual_forwarding(borrowed, arg)),
              std::addressof(borrowed.value));
    arg.value = -1;
    EXPECT_THROW((void)actual_forwarding(borrowed, arg), actual_query_failure);
    EXPECT_THROW((void)borrowed.query(actual_forwarding, arg), actual_query_failure);
    auto verify = [&](auto sender) {
        check_query_values(sender, state);
        EXPECT_EQ(actual_query<true>::default_constructions, 0);
        EXPECT_EQ(actual_query<false>::default_constructions, 0);
        auto attrs = ex::get_env(sender);
        using env_t = decltype(attrs);
        // The classifier answers true on the supplied object; the view
        // constructs no query ([exec.snd.expos]/4).
        static_assert(std::is_invocable_v<const actual_query<true>&, const env_t&>);
        static_assert(!std::is_invocable_v<const actual_query<false>&, const env_t&>);
        EXPECT_EQ(std::addressof(actual_forwarding(attrs)), std::addressof(borrowed.value));
        EXPECT_EQ(actual_query<true>::default_constructions, 0);
        EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
        EXPECT_EQ(state.copies, 0);
        EXPECT_EQ(state.moves, 0);
    };
    for_each_adaptor(source<immovable_attributes, true>{&state, &borrowed}, verify);
    for_each_adaptor(source<immovable_attributes, true, false>{&state, &borrowed}, verify);
}

TEST(ExecutionForwardingAttrs, RawDomainQueryPreservesThrowingMemberAndNoexceptBoundary) {
    attribute_state state;
    auto sender = ex::then(source<>{&state}, value_callback{});
    auto attrs = ex::get_env(sender);
    static_assert(!noexcept(attrs.query(ex::get_completion_domain<>, receiver_env{})));
    static_assert(noexcept(ex::get_completion_domain<>(attrs, receiver_env{})));

    EXPECT_THROW((void)attrs.query(ex::get_completion_domain<>, receiver_env{}), domain_failure);
    EXPECT_EQ(state.raw_domain_calls, 1);
    (void)ex::get_completion_domain<>(attrs, receiver_env{});
    EXPECT_EQ(state.raw_domain_calls, 1);

    // A completion-tag domain is selected from types: the member of the
    // adaptor does not evaluate the member of its child.
    auto raw_value_domain = attrs.query(ex::get_completion_domain<ex::set_value_t>, receiver_env{});
    static_assert(std::is_same_v<decltype(raw_value_domain), tag_domain>);
    static_assert(noexcept(attrs.query(ex::get_completion_domain<ex::set_value_t>, receiver_env{})));
    EXPECT_EQ(state.raw_domain_calls, 1);
    (void)ex::get_completion_domain<ex::set_value_t>(attrs, receiver_env{});
    EXPECT_EQ(state.raw_domain_calls, 1);
}

TEST(ExecutionForwardingAttrs, NestedFiltersPreserveBorrowedReferenceAndCompletion) {
    attribute_state state;
    immovable_attributes borrowed(state);
    auto sender = ex::stopped_as_optional(ex::into_variant(test_bulk::bulk(
        ex::then(source<immovable_attributes, true>{&state, &borrowed}, value_callback{}),
        3, bulk_callback{})));
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
    EXPECT_EQ(state.reads, 1);
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);

    auto result = std::this_thread::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    const auto& payload = std::get<0>(*result);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(std::get<0>(std::get<0>(*payload)), 42);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

TEST(ExecutionForwardingAttrs, NestedFiltersOwnImmovablePrvalueAttributes) {
    attribute_state state;
    auto sender = ex::into_variant(test_bulk::bulk(
        ex::then(source<immovable_attributes>{&state}, value_callback{}), 3, bulk_callback{}));
    {
        auto attrs = ex::get_env(sender);
        EXPECT_EQ(state.alive, 1);
        EXPECT_EQ(state.reads, 1);
        EXPECT_EQ(value_query{}(attrs), 17);
        EXPECT_EQ(state.copies, 0);
        EXPECT_EQ(state.moves, 0);
    }
    EXPECT_EQ(state.alive, 0);
    check_value_completion(std::move(sender), state);
    EXPECT_EQ(state.alive, 0);
}

// ---------------------------------------------------------------------------
// Completion attributes of the single-child adaptors. An adaptor answers a
// completion query in the environment its child is connected with: for a
// possible output tag with COMMON-DOMAIN of every child completion that can
// deliver it, and with a scheduler when all of them come from one site.
// ---------------------------------------------------------------------------

namespace {

using V = ex::set_value_t;
using E = ex::set_error_t;
using S = ex::set_stopped_t;
using loop_scheduler = decltype(std::declval<ex::run_loop&>().get_scheduler());
using stop_env = decltype(ex::prop{std::get_stop_token, std::inplace_stop_token{}});

template<class Tag, class Attrs, class... Envs>
concept has_scheduler = requires(const Attrs& attrs, const Envs&... envs) {
    ex::get_completion_scheduler<Tag>(attrs, envs...);
};

template<class Tag, class Attrs, class... Envs>
concept has_domain = requires(const Attrs& attrs, const Envs&... envs) {
    ex::get_completion_domain<Tag>(attrs, envs...);
};

template<class Tag, class Attrs, class... Envs>
constexpr bool no_answer = !has_scheduler<Tag, Attrs, Envs...> &&
    !has_domain<Tag, Attrs, Envs...>;

template<class Attrs, class... Envs>
constexpr bool no_completion_answers =
    no_answer<V, Attrs, Envs...> && no_answer<E, Attrs, Envs...> && no_answer<S, Attrs, Envs...>;

// The environment is passed with its own value category.
template<class Tag, class Attrs, class Env>
concept scheduler_for = requires(const Attrs& attrs, Env&& env) {
    ex::get_completion_scheduler<Tag>(attrs, static_cast<Env&&>(env));
};

template<class Tag, class Attrs, class Env>
concept domain_for = requires(const Attrs& attrs, Env&& env) {
    ex::get_completion_domain<Tag>(attrs, static_cast<Env&&>(env));
};

struct value_site_domain {};
struct error_site_domain {};

template<class ErrorDomain>
struct site_attrs {
    loop_scheduler first;
    loop_scheduler second;

    auto query(ex::get_completion_scheduler_t<V>) const noexcept { return first; }
    auto query(ex::get_completion_scheduler_t<E>) const noexcept { return second; }
    auto query(ex::get_completion_scheduler_t<S>) const noexcept { return second; }

    value_site_domain query(ex::get_completion_domain_t<V>) const noexcept { return {}; }
    ErrorDomain query(ex::get_completion_domain_t<E>) const noexcept { return {}; }
    ErrorDomain query(ex::get_completion_domain_t<S>) const noexcept { return {}; }
};

// A signature/attribute witness with independently stored site identities.
// It answers every tag, whatever its signatures say.
template<class CS, class ErrorDomain = error_site_domain>
struct site_source {
    using sender_concept = ex::sender_tag;
    site_attrs<ErrorDomain> attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> CS { return {}; }
    auto get_env() const noexcept { return attrs; }
};

struct throwing_conversion {
    throwing_conversion(throwing_conversion&&) noexcept(false) {}
};

struct throwing_callback {
    template<class... Args>
    int operator()(Args&&...) const { return 7; }
};

struct move_only_schedule {
    using sender_concept = ex::sender_tag;
    using child_t = decltype(ex::schedule(std::declval<loop_scheduler>()));

    child_t child;
    std::unique_ptr<int> owner;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures_of_t<child_t, Env> { return {}; }
    auto get_env() const noexcept { return ex::get_env(child); }

    template<ex::receiver R>
    auto connect(R r) && -> ex::connect_result_t<child_t, R> {
        return ex::connect(std::move(child), std::move(r));
    }
};

struct channel_switch : std::forwarding_query_t {};
struct switch_env {
    void query(channel_switch) const noexcept {}
};

// Completes with stopped when the environment carries the switch, and with a
// value otherwise.
struct conditional_source {
    using sender_concept = ex::sender_tag;
    site_attrs<error_site_domain> attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (requires(const Env& env) { env.query(channel_switch{}); }) {
            return ex::completion_signatures<S()>{};
        } else {
            return ex::completion_signatures<V(int)>{};
        }
    }

    auto get_env() const noexcept { return attrs; }
};

struct missing_value_domain : site_attrs<error_site_domain> {
    using site_attrs<error_site_domain>::query;
    void query(ex::get_completion_domain_t<V>) const = delete;
};

struct domainless_value_source {
    using sender_concept = ex::sender_tag;
    missing_value_domain attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<V(int), E(int)> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

} // namespace

namespace {

struct loop_error_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<E>, scheduler};
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<E(int)> {
        return {};
    }

    template<class R>
    struct error_receiver {
        using receiver_concept = ex::receiver_tag;
        R outer;

        void set_value() && noexcept { ex::set_error(std::move(outer), 11); }
        void set_stopped() && noexcept { ex::set_stopped(std::move(outer)); }
        auto get_env() const noexcept -> ex::env<> { return {}; }
    };

    template<ex::receiver R>
    auto connect(R r) const {
        return ex::connect(ex::schedule(scheduler), error_receiver<R>{std::move(r)});
    }
};

struct missing_reader_key : std::forwarding_query_t {};
struct unchecked_reader {
    template<class Env>
    auto operator()(const Env& env) const noexcept {
        return env.query(missing_reader_key{});
    }
};

// A signature probe for a missing receiver key would be a hard error.
struct unchecked_attributed_source {
    using sender_concept = ex::sender_tag;
    site_attrs<error_site_domain> attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using value_t = decltype(std::declval<const Env&>().query(missing_reader_key{}));
        return ex::completion_signatures<V(value_t)>{};
    }

    auto get_env() const noexcept { return attrs; }
};

// The callable below is ill-formed for the argument of the connection that
// the test does not use.
struct unused_value {};

struct increment {
    template<class T>
    auto operator()(T value) const noexcept { return value + 1; }
};

// Usable as an rvalue only. Completes on its advertised scheduler.
struct rvalue_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;
    std::unique_ptr<int> owner;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<
        V(std::conditional_t<std::is_lvalue_reference_v<Self>, unused_value, int>)> {
        return {};
    }

    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<V>, scheduler};
    }

    template<ex::receiver R>
    auto connect(R r) && {
        return ex::connect(ex::then(ex::schedule(scheduler), [] noexcept { return 41; }),
            std::move(r));
    }
};

// Usable as a const lvalue only. Completes on its advertised scheduler.
struct lvalue_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<
        V(std::conditional_t<std::is_lvalue_reference_v<Self>, int, unused_value>)> {
        return {};
    }

    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<V>, scheduler};
    }

    template<ex::receiver R>
    auto connect(R r) const& {
        return ex::connect(ex::then(ex::schedule(scheduler), [] noexcept { return 41; }),
            std::move(r));
    }
    template<class R> void connect(R) && = delete;
};

// Copyable, with an argument that the callable rejects in one connection
// category. Such a sender is only queried here.
template<bool RvalueUsable>
struct one_category_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<
        V(std::conditional_t<std::is_lvalue_reference_v<Self> == RvalueUsable, unused_value, int>),
        E(int)> {
        return {};
    }

    auto get_env() const noexcept {
        return ex::env{ex::prop{ex::get_completion_scheduler<V>, scheduler},
                       ex::prop{ex::get_completion_scheduler<E>, scheduler}};
    }
};

struct category_source {
    using sender_concept = ex::sender_tag;
    site_attrs<value_site_domain> attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (std::is_lvalue_reference_v<Self>) {
            return ex::completion_signatures<E(int)>{};
        } else {
            return ex::completion_signatures<V(int)>{};
        }
    }

    auto get_env() const noexcept { return attrs; }
};

// The same, with different domains at the value and the error site.
struct differing_category_source {
    using sender_concept = ex::sender_tag;
    site_attrs<error_site_domain> attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (std::is_lvalue_reference_v<Self>) {
            return ex::completion_signatures<E(int)>{};
        } else {
            return ex::completion_signatures<V(int)>{};
        }
    }

    auto get_env() const noexcept { return attrs; }
};

struct borrowed_env {};
struct env_category_source {
    using sender_concept = ex::sender_tag;
    site_attrs<error_site_domain> attrs;

    auto get_completion_signatures(borrowed_env&) const noexcept
        -> ex::completion_signatures<S()> { return {}; }
    auto get_completion_signatures(const borrowed_env&) const noexcept
        -> ex::completion_signatures<V(int)> { return {}; }
    auto get_completion_signatures(borrowed_env&&) const noexcept
        -> ex::completion_signatures<V(int)> { return {}; }

    auto get_env() const noexcept { return attrs; }
};

struct terminal_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const terminal_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

struct volatile_scheduler_attrs {
    volatile terminal_scheduler* scheduler;
    auto query(ex::get_completion_scheduler_t<V>) const noexcept -> volatile terminal_scheduler& {
        return *scheduler;
    }
};

struct volatile_scheduler_source {
    using sender_concept = ex::sender_tag;
    volatile_scheduler_attrs attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

struct const_scheduler_source {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<V>, terminal_scheduler{}};
    }
};

// Moving from it leaves another resource behind.
struct consumable_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;

    explicit consumable_scheduler(int r) noexcept : resource(r) {}
    consumable_scheduler(const consumable_scheduler&) noexcept = default;
    consumable_scheduler(consumable_scheduler&& other) noexcept
        : resource(std::exchange(other.resource, 0)) {}
    consumable_scheduler& operator=(const consumable_scheduler&) noexcept = default;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const consumable_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

struct expiring_scheduler_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        consumable_scheduler* scheduler;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept -> consumable_scheduler&& {
            return std::move(*scheduler);
        }
    };

    attributes attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

// Its value query answers only on an operand that is not volatile, and it
// names another resource.
struct rerouting_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;

    rerouting_scheduler() = default;
    explicit rerouting_scheduler(int r) noexcept : resource(r) {}
    rerouting_scheduler(const rerouting_scheduler&) = default;
    rerouting_scheduler(const volatile rerouting_scheduler& other) noexcept
        : resource(other.resource) {}
    rerouting_scheduler(volatile rerouting_scheduler&& other) noexcept
        : resource(other.resource) {}
    rerouting_scheduler& operator=(const rerouting_scheduler&) = default;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const rerouting_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<V>) const noexcept {
        return rerouting_scheduler{1};
    }
};

struct volatile_prvalue_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        auto query(ex::get_completion_scheduler_t<V>) const noexcept -> volatile rerouting_scheduler {
            return rerouting_scheduler{0};
        }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept -> attributes { return {}; }
};

struct copy_nothrow_move_throwing {
    copy_nothrow_move_throwing() = default;
    copy_nothrow_move_throwing(const copy_nothrow_move_throwing&) noexcept = default;
    copy_nothrow_move_throwing(copy_nothrow_move_throwing&&) noexcept(false) { throw 42; }
};

// Like consumable_scheduler, with a unary value query that names itself. The
// public CPO resolves such a result by constructing a scheduler from it.
struct recursive_consumable_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;

    explicit recursive_consumable_scheduler(int r) noexcept : resource(r) {}
    recursive_consumable_scheduler(const recursive_consumable_scheduler&) noexcept = default;
    recursive_consumable_scheduler(recursive_consumable_scheduler&& other) noexcept
        : resource(std::exchange(other.resource, 0)) {}
    recursive_consumable_scheduler& operator=(const recursive_consumable_scheduler&) noexcept = default;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const recursive_consumable_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<V>) const noexcept { return *this; }
};

struct recursive_expiring_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        recursive_consumable_scheduler* scheduler;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept
            -> recursive_consumable_scheduler&& {
            return std::move(*scheduler);
        }
    };

    attributes attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

// On a volatile scheduler the value query returns a value. On a scheduler
// that is not volatile it returns an rvalue reference to another scheduler.
struct two_faced_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;
    two_faced_scheduler* lender = nullptr;

    explicit two_faced_scheduler(int r, two_faced_scheduler* l = nullptr) noexcept
        : resource(r), lender(l) {}
    two_faced_scheduler(const two_faced_scheduler&) noexcept = default;
    two_faced_scheduler(two_faced_scheduler&& other) noexcept
        : resource(std::exchange(other.resource, 0)), lender(other.lender) {}
    two_faced_scheduler(const volatile two_faced_scheduler& other) noexcept
        : resource(other.resource), lender(other.lender) {}
    two_faced_scheduler(volatile two_faced_scheduler&& other) noexcept
        : resource(other.resource), lender(other.lender) {}
    two_faced_scheduler& operator=(const two_faced_scheduler&) noexcept = default;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const two_faced_scheduler& other) const noexcept {
        return resource == other.resource;
    }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<V>) const volatile noexcept -> two_faced_scheduler {
        return two_faced_scheduler{resource, lender};
    }
    auto query(ex::get_completion_scheduler_t<V>) const noexcept -> two_faced_scheduler&& {
        return std::move(*lender);
    }
};

struct two_faced_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        two_faced_scheduler* lender;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept
            -> volatile two_faced_scheduler {
            return two_faced_scheduler{1, lender};
        }
    };

    attributes attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

struct hop_target_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const hop_target_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

// Its unary value query names another scheduler type. It has no stopped
// query, so the public CPO answers a stopped query on it with the operand
// itself, without resolving that value query.
struct hopping_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const hopping_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<V>) const noexcept {
        return hop_target_scheduler{9};
    }
};

// A sender whose attributes are a scheduler.
template<class Scheduler>
struct scheduler_attrs_source {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<V(), S()> { return {}; }
    auto get_env() const noexcept -> Scheduler { return {}; }
};

} // namespace

namespace {

struct passive_domain {};
template<class Sender>
concept callable_adaptor = requires {
    typename std::remove_cvref_t<Sender>::source_t;
    typename std::remove_cvref_t<Sender>::fn_t;
};

// Replaces a then sender, and nothing else, when it is connected. The
// origin below it is left alone.
struct then_replacing_domain {
    template<callable_adaptor Sender, class Env>
    auto transform_sender(ex::set_value_t, Sender&&, const Env&) const noexcept {
        return ex::just_stopped();
    }
};

template<class Domain>
struct domain_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    struct attributes {
        loop_scheduler scheduler;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept { return scheduler; }
        Domain query(ex::get_completion_domain_t<V>) const noexcept { return {}; }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept -> attributes { return {scheduler}; }
};

// Value and stopped, one domain for both, which replaces callable adaptors.
struct mixed_domain_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    struct attributes {
        loop_scheduler scheduler;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept { return scheduler; }
        auto query(ex::get_completion_scheduler_t<S>) const noexcept { return scheduler; }
        then_replacing_domain query(ex::get_completion_domain_t<V>) const noexcept { return {}; }
        then_replacing_domain query(ex::get_completion_domain_t<S>) const noexcept { return {}; }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<V(), S()> { return {}; }
    auto get_env() const noexcept -> attributes { return {scheduler}; }
};

} // namespace

namespace {

struct derived_domain {};

// A sender derived from an adaptor. Its attributes and its delivery are its
// own; the node that its base declares describes the base only.
template<class Base>
struct derived_adaptor : Base {
    loop_scheduler own;

    struct attributes {
        loop_scheduler scheduler;
        auto query(ex::get_completion_scheduler_t<V>) const noexcept { return scheduler; }
        derived_domain query(ex::get_completion_domain_t<V>) const noexcept { return {}; }
    };

    auto get_env() const noexcept -> attributes { return {own}; }
};

// A scheduler only as an lvalue.
struct lvalue_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() const& noexcept { return ex::schedule(ex::inline_scheduler{}); }
    void schedule() && = delete;
    bool operator==(const lvalue_scheduler&) const noexcept = default;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

// Its attributes are a prvalue of that scheduler type.
struct prvalue_scheduler_attrs_source {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V()> { return {}; }
    auto get_env() const noexcept -> lvalue_scheduler { return {}; }
};

template<class Tag, class Sender, class Env>
concept direct_scheduler = requires(const Sender& sender, const Env& env) {
    ex::get_completion_scheduler<Tag>(ex::get_env(sender), env);
};

template<class Tag, class Sender, class Env>
concept direct_domain = requires(const Sender& sender, const Env& env) {
    ex::get_completion_domain<Tag>(ex::get_env(sender), env);
};

} // namespace

TEST(ExecutionForwardingAttrs, ThenKeepsThePossibleTagsOfAMoveOnlyChild) {
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();
    int calls = 0;
    auto sender = ex::then(move_only_schedule{ex::schedule(scheduler), std::make_unique<int>(1)},
        [&] { return ++calls; });
    static_assert(!std::copy_constructible<decltype(sender)>);
    const auto attrs = ex::get_env(sender);
    using attrs_t = decltype(attrs);
    // No unary completion answer, and no stopped answer without a stoppable token.
    static_assert(no_completion_answers<attrs_t>);
    static_assert(no_answer<S, attrs_t, ex::env<>>);
    // The child cannot complete with an error. Whether the callable adds one
    // at the value site is not examined, so error has no answer.
    static_assert(no_answer<E, attrs_t, ex::env<>>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
        ex::default_domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<S>(
        attrs, stop_env{std::get_stop_token, {}})), ex::default_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<S>(attrs, stop_env{std::get_stop_token, {}}), scheduler);
    EXPECT_EQ(calls, 0);
}

TEST(ExecutionForwardingAttrs, RecoveredCompletionTakesTheSiteOfTheConsumedChannel) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    // The source answers every tag, but its only completion is an error: the
    // recovered value arrives where the error did.
    const auto error = site_source<ex::completion_signatures<E(int)>>{{a, b}};
    const auto recovered_sender = ex::upon_error(error, value_callback{});
    const auto recovered = ex::get_env(recovered_sender);
    using recovered_t = decltype(recovered);
    static_assert(no_completion_answers<recovered_t>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(recovered, ex::env<>{})),
        error_site_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(recovered, ex::env<>{}), b);
    static_assert(no_answer<S, recovered_t, ex::env<>>);
    // An error remains only if the handler throws, which is not examined.
    static_assert(no_answer<E, recovered_t, ex::env<>>);
    using fallible = ex::env_of_t<decltype(ex::upon_error(error, throwing_callback{}))>;
    static_assert(no_answer<E, fallible, ex::env<>>);

    const auto stopped = site_source<ex::completion_signatures<S()>>{{a, b}};
    const auto resumed_sender = ex::upon_stopped(stopped, value_callback{});
    const auto resumed = ex::get_env(resumed_sender);
    static_assert(no_answer<S, decltype(resumed), ex::env<>>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(resumed, ex::env<>{})),
        error_site_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(resumed, ex::env<>{}), b);

    const auto as_error_sender = ex::stopped_as_error(stopped, 42);
    const auto as_error = ex::get_env(as_error_sender);
    static_assert(no_completion_answers<decltype(as_error)>);
    static_assert(no_answer<S, decltype(as_error), ex::env<>>);
    static_assert(no_answer<V, decltype(as_error), ex::env<>>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(as_error, ex::env<>{})),
        error_site_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<E>(as_error, ex::env<>{}), b);

    const auto optional_sender = ex::stopped_as_optional(stopped);
    static_assert(no_answer<S, ex::env_of_t<decltype(optional_sender)>, ex::env<>>);
}

TEST(ExecutionForwardingAttrs, RecoveredValueIsDeliveredAtTheReportedSite) {
    ex::run_loop first, second;
    auto sender = ex::upon_error(loop_error_source{first.get_scheduler()}, value_callback{});
    EXPECT_EQ(ex::get_completion_scheduler<V>(ex::get_env(sender), receiver_env{}),
        first.get_scheduler());
    result_state result;
    auto op = ex::connect(std::move(sender), receiver{&result});
    ex::start(op);
    second.finish();
    second.run();
    EXPECT_EQ(result.values + result.errors + result.stopped, 0);
    first.finish();
    first.run();
    EXPECT_EQ(result.values, 1);
    EXPECT_EQ(result.errors + result.stopped, 0);
}

TEST(ExecutionForwardingAttrs, SeveralSitesGiveTheCommonDomainAndNoScheduler) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    ASSERT_NE(a, b);
    using value_error = ex::completion_signatures<V(int), E(int)>;
    using both_domains = ex::indeterminate_domain<value_site_domain, error_site_domain>;

    // Value and error sites with different domains.
    const auto distinct = site_source<value_error>{{a, b}};
    const auto then_sender = ex::then(distinct, throwing_callback{});
    const auto then_attrs = ex::get_env(then_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(then_attrs, ex::env<>{})),
        value_site_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(then_attrs, ex::env<>{}), a);
    // The error domain is the error site's, or both if the callable throws.
    // That is not examined, so there is no answer.
    static_assert(no_answer<E, decltype(then_attrs), ex::env<>>);
    // Recovery delivers the value at both sites.
    const auto upon_sender = ex::upon_error(distinct, value_callback{});
    const auto upon_attrs = ex::get_env(upon_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(upon_attrs, ex::env<>{})),
        both_domains>);
    static_assert(!has_scheduler<V, decltype(upon_attrs), ex::env<>>);
    static_assert(no_answer<E, decltype(upon_attrs), ex::env<>>);

    // One domain at every site: the answer does not depend on the callable.
    // Two sites of one sender are still two schedulers.
    const auto common = site_source<value_error, value_site_domain>{{a, b}};
    const auto common_then_sender = ex::then(common, throwing_callback{});
    const auto common_then = ex::get_env(common_then_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(common_then, ex::env<>{})),
        value_site_domain>);
    static_assert(!has_scheduler<E, decltype(common_then), ex::env<>>);
    const auto common_upon_sender = ex::upon_error(common, value_callback{});
    const auto common_upon = ex::get_env(common_upon_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(common_upon, ex::env<>{})),
        value_site_domain>);
    static_assert(!has_scheduler<V, decltype(common_upon), ex::env<>>);

    using all = ex::completion_signatures<V(int), E(int), S()>;
    const auto child = site_source<all, value_site_domain>{{a, b}};
    const auto upon_stop_sender = ex::upon_stopped(child, value_callback{});
    const auto upon_stop = ex::get_env(upon_stop_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(upon_stop, ex::env<>{})),
        value_site_domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(upon_stop, ex::env<>{})),
        value_site_domain>);
    static_assert(!has_scheduler<V, decltype(upon_stop), ex::env<>>);
    static_assert(!has_scheduler<E, decltype(upon_stop), ex::env<>>);
    static_assert(no_answer<S, decltype(upon_stop), ex::env<>>);

    const auto optional_sender = ex::stopped_as_optional(site_source<all>{{a, b}});
    const auto optional = ex::get_env(optional_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(optional, ex::env<>{})),
        both_domains>);
    static_assert(!has_scheduler<V, decltype(optional), ex::env<>>);
    static_assert(no_answer<S, decltype(optional), ex::env<>>);

    const auto error_sender = ex::stopped_as_error(child, 42);
    const auto error = ex::get_env(error_sender);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(error, ex::env<>{})),
        value_site_domain>);
    static_assert(!has_scheduler<E, decltype(error), ex::env<>>);
    static_assert(no_answer<S, decltype(error), ex::env<>>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(error, ex::env<>{}), a);

    // A site without a domain leaves the output without one; a scheduler
    // without a domain stays a scheduler without a domain.
    const auto opaque_sender = ex::then(domainless_value_source{{{a, b}}}, throwing_callback{});
    const auto opaque = ex::get_env(opaque_sender);
    static_assert(no_answer<E, decltype(opaque), ex::env<>>);
    static_assert(!has_domain<V, decltype(opaque), ex::env<>>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(opaque, ex::env<>{}), a);
    using opaque_recovery = ex::env_of_t<decltype(ex::upon_error(
        domainless_value_source{{{a, b}}}, value_callback{}))>;
    static_assert(no_answer<V, opaque_recovery, ex::env<>>);
}

TEST(ExecutionForwardingAttrs, OneOriginSiteKeepsItsSchedulerThroughAChain) {
    ex::run_loop loop;
    const auto a = loop.get_scheduler();
    // The value and the possible error of then both come from the value site
    // of the schedule sender, so the following recovery has one site.
    const auto sender = ex::upon_error(ex::then(ex::schedule(a), throwing_callback{}), value_callback{});
    const auto attrs = ex::get_env(sender);
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), a);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
        ex::default_domain>);
    static_assert(no_answer<E, decltype(attrs), ex::env<>>);
    // A stoppable environment adds the stopped site, which then passes through.
    const stop_env stoppable{std::get_stop_token, {}};
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, stoppable), a);
    EXPECT_EQ(ex::get_completion_scheduler<S>(attrs, stoppable), a);
    // Recovering it makes two sites deliver the value: one domain, and no
    // scheduler, because two tags of one sender are two sites.
    const auto resumed_sender = ex::upon_stopped(sender, value_callback{});
    const auto resumed = ex::get_env(resumed_sender);
    static_assert(!has_scheduler<V, decltype(resumed), stop_env>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(resumed, stoppable)),
        ex::default_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(resumed, ex::env<>{}), a);
}

TEST(ExecutionForwardingAttrs, ConversionsAndBulkKeepTheValueSite) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto conversion = site_source<ex::completion_signatures<V(throwing_conversion)>>{{a, b}};
    const auto optional_sender = ex::stopped_as_optional(conversion);
    const auto optional = ex::get_env(optional_sender);
    const auto variant_sender = ex::into_variant(conversion);
    const auto variant = ex::get_env(variant_sender);
    // An error exists only if the conversion throws, which is not examined.
    static_assert(no_answer<E, decltype(optional), ex::env<>>);
    static_assert(no_answer<E, decltype(variant), ex::env<>>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(optional, ex::env<>{}), a);
    EXPECT_EQ(ex::get_completion_scheduler<V>(variant, ex::env<>{}), a);

    const auto child = site_source<ex::completion_signatures<V(int)>>{{a, b}};
    auto check = [&](const auto& sender) {
        const auto attrs = ex::get_env(sender);
        using attrs_t = decltype(attrs);
        static_assert(no_completion_answers<attrs_t>);
        static_assert(no_answer<E, attrs_t, ex::env<>>);
        static_assert(no_answer<S, attrs_t, ex::env<>>);
        static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
            value_site_domain>);
        EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), a);
    };
    check(test_bulk::bulk(child, 2, throwing_callback{}));
    check(test_bulk::bulk_chunked(child, 2, throwing_callback{}));
    check(test_bulk::bulk_unchunked(child, 2, throwing_callback{}));
    check(test_bulk::bulk_unchunked(child, 2, [](int, int&) noexcept {}));

#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
    check(ex::bulk(child, ex::seq, 2, throwing_callback{}));
    check(ex::bulk_chunked(child, ex::seq, 2, throwing_callback{}));
    check(ex::bulk_unchunked(child, ex::seq, 2, throwing_callback{}));
#endif
}

TEST(ExecutionForwardingAttrs, EnvironmentDecidesTheChannelAndUnaryCannotBringItBack) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto sender = ex::then(conditional_source{{a, b}}, value_callback{});
    const auto attrs = ex::get_env(sender);
    using attrs_t = decltype(attrs);
    static_assert(no_completion_answers<attrs_t>);
    static_assert(no_answer<S, attrs_t, ex::env<>>);
    static_assert(no_answer<V, attrs_t, switch_env>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
        value_site_domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<S>(attrs, switch_env{})),
        error_site_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), a);
    EXPECT_EQ(ex::get_completion_scheduler<S>(attrs, switch_env{}), b);

    // Ordinary attributes keep the unary retry of the public CPOs for a pack
    // of environments. The adaptor has no member for such a pack.
    const auto raw = ex::env{site_attrs<error_site_domain>{a, b}};
    EXPECT_EQ(ex::get_completion_scheduler<V>(raw, ex::env<>{}, switch_env{}), a);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(
        raw, ex::env<>{}, switch_env{})), error_site_domain>);
    static_assert(no_completion_answers<attrs_t, ex::env<>, switch_env>);

    // Wrapping the attributes cannot bring a ruled-out channel back.
    const auto wrapped = ex::env{ex::get_env(sender)};
    using wrapped_t = decltype(wrapped);
    EXPECT_EQ(ex::get_completion_scheduler<V>(wrapped, ex::env<>{}), a);
    EXPECT_EQ(ex::get_completion_scheduler<S>(wrapped, switch_env{}), b);
    static_assert(no_completion_answers<wrapped_t>);
    static_assert(no_answer<V, wrapped_t, switch_env>);
    static_assert(no_answer<S, wrapped_t, ex::env<>>);
    static_assert(no_completion_answers<wrapped_t, ex::env<>, switch_env>);
}

TEST(ExecutionForwardingAttrs, VolatileEnvironmentCannotDisagreeWithPublicSignatures) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto sender = ex::then(conditional_source{{a, b}}, value_callback{});
    // The signature CPO removes volatile, so the public signatures of the
    // adaptor are stopped-only, while a receiver that returns the environment
    // as volatile would connect a value-only child.
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), switch_env>,
        ex::completion_signatures<S()>>);
    using attrs_t = ex::env_of_t<decltype(sender)>;
    static_assert(scheduler_for<S, attrs_t, switch_env&>);
    static_assert(scheduler_for<S, attrs_t, const switch_env&>);
    static_assert(!scheduler_for<V, attrs_t, volatile switch_env&>);
    static_assert(!domain_for<V, attrs_t, volatile switch_env&>);
    static_assert(!scheduler_for<S, attrs_t, volatile switch_env&>);
    static_assert(!domain_for<S, attrs_t, volatile switch_env&>);
    static_assert(!scheduler_for<V, attrs_t, const volatile switch_env&>);
    static_assert(!domain_for<S, attrs_t, const volatile switch_env&>);
    // The same holds below an environment overlay and for nested adaptors.
    using nested_t = ex::env_of_t<decltype(ex::write_env(
        ex::upon_error(sender, value_callback{}), ex::env<>{}))>;
    static_assert(scheduler_for<S, nested_t, const switch_env&>);
    static_assert(!scheduler_for<V, nested_t, volatile switch_env&>);
    static_assert(!domain_for<S, nested_t, volatile switch_env&>);
}

TEST(ExecutionForwardingAttrs, UnanswerableQueriesDoNotProbeSignatures) {
    // read_env has no attributes, and its signatures cannot be formed here.
    const auto opaque = ex::then(ex::read_env(unchecked_reader{}), value_callback{});
    using opaque_attrs = ex::env_of_t<decltype(opaque)>;
    static_assert(no_completion_answers<opaque_attrs>);
    static_assert(no_completion_answers<opaque_attrs, ex::env<>>);
    using nested_attrs = ex::env_of_t<decltype(ex::write_env(
        ex::upon_error(ex::stopped_as_optional(opaque), value_callback{}), ex::env<>{}))>;
    static_assert(no_completion_answers<nested_attrs, ex::env<>>);
    using outer_opaque = ex::env_of_t<decltype(ex::then(ex::then(
        ex::read_env(unchecked_reader{}), value_callback{}), value_callback{}))>;
    static_assert(no_completion_answers<outer_opaque, ex::env<>>);

    // A channel that no edge of the chain can produce needs no signature
    // either, although the source below answers the tag. The source's
    // signatures cannot be formed in this environment.
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();
    const auto source = unchecked_attributed_source{{scheduler, scheduler}};
    static_assert(has_scheduler<S, ex::env_of_t<decltype(source)>, ex::env<>>);
    const auto intercepted = ex::upon_stopped(source, value_callback{});
    static_assert(no_answer<S, ex::env_of_t<decltype(intercepted)>, ex::env<>>);
    using outer_stopped = ex::env_of_t<decltype(ex::then(intercepted, value_callback{}))>;
    static_assert(no_answer<S, outer_stopped, ex::env<>>);
    using outer_optional = ex::env_of_t<decltype(test_bulk::bulk(
        ex::stopped_as_optional(source), 2, bulk_callback{}))>;
    static_assert(no_answer<S, outer_optional, ex::env<>>);
    using outer_as_error = ex::env_of_t<decltype(ex::schedule_from(ex::write_env(
        ex::stopped_as_error(source, 1), ex::env<>{})))>;
    static_assert(no_answer<S, outer_as_error, ex::env<>>);

    // An error after upon_error exists only if the handler throws, which is
    // never examined: no chain of certain edges reaches it, so it has no
    // answer and nothing is asked, here and above further adaptors.
    const auto recovered = ex::upon_error(source, value_callback{});
    static_assert(no_answer<E, ex::env_of_t<decltype(recovered)>, ex::env<>>);
    using outer_error = ex::env_of_t<decltype(ex::write_env(
        ex::into_variant(ex::then(recovered, value_callback{})), ex::env<>{}))>;
    static_assert(no_answer<E, outer_error, ex::env<>>);
    using outer_error_recovery = ex::env_of_t<decltype(ex::upon_stopped(
        ex::schedule_from(recovered), value_callback{}))>;
    static_assert(no_answer<E, outer_error_recovery, ex::env<>>);
}

TEST(ExecutionForwardingAttrs, QueriesNeverDeduceACallableForEitherConnection) {
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();

    // The body of increment is ill-formed for the argument of one connection
    // category. Every completion query is still answered or absent.
    auto check = [&](const auto& sender) {
        const auto attrs = ex::get_env(sender);
        using attrs_t = decltype(attrs);
        static_assert(no_completion_answers<attrs_t>);
        static_assert(!has_scheduler<E, attrs_t, receiver_env>);
        static_assert(no_answer<S, attrs_t, receiver_env>);
        EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, receiver_env{}), scheduler);
    };
    check(ex::then(one_category_source<true>{scheduler}, increment{}));
    check(ex::then(one_category_source<false>{scheduler}, increment{}));
    check(test_bulk::bulk(ex::upon_stopped(
        ex::then(one_category_source<false>{scheduler}, increment{}), value_callback{}),
        2, bulk_callback{}));

    // A move-only sender has the rvalue connection only, and a sender whose
    // child connects as a const lvalue only is used as a const lvalue. Both
    // complete on the scheduler that their attributes name.
    auto rvalue = ex::then(rvalue_source{scheduler, std::make_unique<int>(1)}, increment{});
    static_assert(no_answer<E, ex::env_of_t<decltype(rvalue)>, receiver_env>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(ex::get_env(rvalue), receiver_env{}), scheduler);
    result_state first;
    auto op1 = ex::connect(std::move(rvalue), receiver{&first});
    ex::start(op1);

    const auto lvalue = ex::then(lvalue_source{scheduler}, increment{});
    static_assert(no_answer<E, ex::env_of_t<decltype(lvalue)>, receiver_env>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(ex::get_env(lvalue), receiver_env{}), scheduler);
    result_state second;
    auto op2 = ex::connect(lvalue, receiver{&second});
    ex::start(op2);

    EXPECT_EQ(first.values + second.values, 0);
    loop.finish();
    loop.run();
    EXPECT_EQ(first.values, 1);
    EXPECT_EQ(second.values, 1);
    EXPECT_EQ(first.errors + first.stopped + second.errors + second.stopped, 0);
}

TEST(ExecutionForwardingAttrs, ConnectionCategoriesMustAgreeOnTheAnswer) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    // An rvalue connection delivers the value of the child; a const-lvalue
    // connection recovers its error. The attributes cannot tell which one
    // will be used. Both deliver a value in the same domain, which is the
    // answer; they deliver it at different sites, so there is no scheduler.
    const auto categorized = ex::upon_error(category_source{{a, b}}, value_callback{});
    const auto attrs = ex::get_env(categorized);
    using attrs_t = decltype(attrs);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
        value_site_domain>);
    static_assert(!has_scheduler<V, attrs_t, ex::env<>>);
    static_assert(no_answer<E, attrs_t, ex::env<>>);
    static_assert(no_answer<S, attrs_t, ex::env<>>);
    // With different domains at the two sites there is no common answer.
    using differing = ex::env_of_t<decltype(ex::upon_error(
        differing_category_source{{a, b}}, value_callback{}))>;
    static_assert(no_completion_answers<differing, ex::env<>>);
    // then keeps each channel, but no channel exists in both connections.
    using kept = ex::env_of_t<decltype(ex::then(category_source{{a, b}}, value_callback{}))>;
    static_assert(no_completion_answers<kept, ex::env<>>);
}

TEST(ExecutionForwardingAttrs, StoppedAsErrorWithholdsBorrowedEnvironmentConflict) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto sender = ex::stopped_as_error(env_category_source{{a, b}}, 42);
    const auto attrs = ex::get_env(sender);
    using attributes = decltype(attrs);
    using value_only = ex::completion_signatures<V(int)>;
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), borrowed_env&>, value_only>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), const borrowed_env&>, value_only>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), borrowed_env>, value_only>);
    static_assert(!scheduler_for<E, attributes, borrowed_env&> && !domain_for<E, attributes, borrowed_env&>);
    static_assert(!scheduler_for<E, attributes, const borrowed_env&> && !domain_for<E, attributes, const borrowed_env&>);
    static_assert(!scheduler_for<E, attributes, borrowed_env> && !domain_for<E, attributes, borrowed_env>);
    static_assert(!scheduler_for<V, attributes, borrowed_env&> && !domain_for<V, attributes, borrowed_env&>);
    static_assert(domain_for<V, attributes, const borrowed_env&> && domain_for<V, attributes, borrowed_env>);
    const borrowed_env environment;
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, environment), a);
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, borrowed_env{}), a);
}

TEST(ExecutionForwardingAttrs, BorrowedSchedulerIsCopiedFromAConstLvalueOrWithheld) {
    // A borrowed scheduler is published as a copy, because the result must
    // not refer to the view that the attributes pass to their child.
    const auto owned = const_scheduler_source{};
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(ex::get_env(owned), ex::env<>{})),
        const terminal_scheduler&>);
    const auto through_sender = ex::then(owned, value_callback{});
    const auto through = ex::get_env(through_sender);
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(through, ex::env<>{})),
        terminal_scheduler>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(through, ex::env<>{}), terminal_scheduler{});

    // An rvalue-reference result is copied through a const lvalue, so a query
    // never moves from the scheduler that the source keeps.
    consumable_scheduler kept{7};
    const auto expiring = expiring_scheduler_source{{&kept}};
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(ex::get_env(expiring), ex::env<>{})),
        consumable_scheduler&&>);
    const auto expiring_then = ex::then(expiring, value_callback{});
    const auto expiring_attrs = ex::get_env(expiring_then);
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(expiring_attrs, ex::env<>{})),
        consumable_scheduler>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(expiring_attrs, ex::env<>{}).resource, 7);
    EXPECT_EQ(ex::get_completion_scheduler<V>(expiring_attrs, ex::env<>{}).resource, 7);
    EXPECT_EQ(kept.resource, 7);

    // If the scheduler also has a value query of its own, the public CPO
    // resolves the borrowed result by constructing a scheduler from the
    // rvalue reference. The adaptor does not start that resolution.
    recursive_consumable_scheduler lent{5};
    const auto recursive = recursive_expiring_source{{&lent}};
    static_assert(has_scheduler<V, ex::env_of_t<decltype(recursive)>, ex::env<>>);
    using recursive_then = ex::env_of_t<decltype(ex::then(recursive, value_callback{}))>;
    static_assert(!has_scheduler<V, recursive_then, ex::env<>>);
    using recursive_nested = ex::env_of_t<decltype(ex::write_env(
        ex::upon_error(ex::into_variant(recursive), value_callback{}), ex::env<>{}))>;
    static_assert(!has_scheduler<V, recursive_nested, ex::env<>>);
    EXPECT_EQ(lent.resource, 5);

    // The resolver queries an unqualified copy of a result, not the result
    // it was given. Here the volatile result's own value query returns a
    // value, while the copy's returns an rvalue reference to a scheduler it
    // only lends, from which the resolver would construct. The adaptor does
    // not start that resolution either.
    two_faced_scheduler other{3};
    const auto two_faced = two_faced_source{{&other}};
    using two_faced_then = ex::env_of_t<decltype(ex::then(two_faced, value_callback{}))>;
    static_assert(!has_scheduler<V, two_faced_then, ex::env<>>);
    using two_faced_nested = ex::env_of_t<decltype(ex::stopped_as_error(
        ex::write_env(two_faced, ex::env<>{}), 1))>;
    static_assert(!has_scheduler<V, two_faced_nested, ex::env<>>);
    EXPECT_EQ(other.resource, 3);

    // A copy would remove volatile from a borrowed volatile scheduler, which
    // changes the members that answer on it. It has no answer.
    volatile terminal_scheduler scheduler;
    const auto borrowed = volatile_scheduler_source{{&scheduler}};
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(ex::get_env(borrowed), ex::env<>{})),
        volatile terminal_scheduler&>);
    static_assert(!has_scheduler<V, ex::env_of_t<decltype(ex::then(borrowed, value_callback{}))>, ex::env<>>);
    static_assert(!has_scheduler<V, ex::env_of_t<decltype(ex::stopped_as_error(borrowed, 1))>, ex::env<>>);
}

TEST(ExecutionForwardingAttrs, PublishedSchedulerIsTheOneThatThePublicCpoResolvesAgain) {
    // A volatile prvalue is published as it is. As an unqualified scheduler
    // it would let the public CPO ask its value query, which is unavailable
    // on the volatile result and names another resource.
    const auto qualified = volatile_prvalue_source{};
    auto check_qualified = [](const auto& sender) {
        const auto attrs = ex::get_env(sender);
        static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(attrs, ex::env<>{})),
            volatile rerouting_scheduler>);
        const rerouting_scheduler result = ex::get_completion_scheduler<V>(attrs, ex::env<>{});
        EXPECT_EQ(result.resource, 0);
    };
    check_qualified(qualified);
    check_qualified(ex::then(qualified, value_callback{}));
    check_qualified(ex::stopped_as_error(qualified, 1));
    check_qualified(ex::write_env(ex::into_variant(qualified), ex::env<>{}));
    EXPECT_EQ(rerouting_scheduler{}.query(ex::get_completion_scheduler<V>).resource, 1);

    // Attributes that are a scheduler answer a tag they have no member for
    // with the scheduler itself, unresolved. Published by an adaptor, that
    // result would be resolved by the public CPO: through the scheduler's
    // value query it would become another scheduler. It is withheld.
    const auto hopping = scheduler_attrs_source<hopping_scheduler>{};
    const stop_env stoppable{std::get_stop_token, {}};
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<S>(ex::get_env(hopping), stoppable)),
        hopping_scheduler>);
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<V>(ex::get_env(hopping), stoppable)),
        hop_target_scheduler>);
    using hopping_then = ex::env_of_t<decltype(ex::then(hopping, value_callback{}))>;
    static_assert(!has_scheduler<S, hopping_then, stop_env>);
    using hopping_nested = ex::env_of_t<decltype(ex::write_env(
        ex::schedule_from(hopping), ex::env<>{}))>;
    static_assert(!has_scheduler<S, hopping_nested, stop_env>);
    // The value answer was resolved by the child's CPO and is stable.
    const auto hopping_sender = ex::then(hopping, value_callback{});
    EXPECT_EQ(ex::get_completion_scheduler<V>(ex::get_env(hopping_sender), stoppable).resource, 9);

    // Without a value query nothing can change, and the operand is published.
    const auto terminal = scheduler_attrs_source<terminal_scheduler>{};
    const auto terminal_sender = ex::then(terminal, value_callback{});
    const auto terminal_attrs = ex::get_env(terminal_sender);
    static_assert(std::same_as<decltype(ex::get_completion_scheduler<S>(terminal_attrs, stoppable)),
        terminal_scheduler>);
    EXPECT_EQ(ex::get_completion_scheduler<S>(terminal_attrs, stoppable), terminal_scheduler{});
    EXPECT_EQ(ex::get_completion_scheduler<V>(terminal_attrs, stoppable), terminal_scheduler{});
}

TEST(ExecutionForwardingAttrs, ValueDomainSelectsLateCustomizationAsBefore) {
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();
    using value_only = ex::completion_signatures<V()>;
    using stopped_only = ex::completion_signatures<S()>;

    // connect selects the completion domain from the value domain that the
    // adaptor reports. A domain without a transform changes nothing.
    const auto kept = ex::write_env(
        ex::then(domain_source<passive_domain>{scheduler}, value_callback{}), ex::env<>{});
    const auto kept_attrs = ex::get_env(kept);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(kept_attrs, ex::env<>{})),
        passive_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(kept_attrs, ex::env<>{}), scheduler);

    // A domain that replaces then is selected and applied, as at c29cc68.
    // The source below is not replaced. The attributes describe the
    // untransformed sender: the value domain that then reports is what
    // selects the transform, and nothing in the attributes of then, or of an
    // adaptor above it, follows the replacement.
    const auto origin = domain_source<then_replacing_domain>{scheduler};
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(origin), ex::env<>>, value_only>);
    const auto targeted = ex::then(origin, value_callback{});
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(
        ex::get_env(targeted), ex::env<>{})), then_replacing_domain>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(targeted), ex::env<>>, stopped_only>);

    using above_t = decltype(ex::schedule_from(targeted));
    static_assert(std::same_as<ex::completion_signatures_of_t<above_t, ex::env<>>, stopped_only>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(
        std::declval<const ex::env_of_t<above_t>&>(), ex::env<>{})), then_replacing_domain>);

    // The same holds when the value is partly a recovered completion: the
    // common domain of both sites is that domain, and it is still selected.
    const auto mixed = ex::upon_stopped(mixed_domain_source{scheduler}, value_callback{});
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(
        ex::get_env(mixed), ex::env<>{})), then_replacing_domain>);
    static_assert(!has_scheduler<V, ex::env_of_t<decltype(mixed)>, ex::env<>>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(mixed), ex::env<>>, stopped_only>);
    // The replacement is what connects: the receiver gets stopped at once.
    result_state result;
    auto op = ex::connect(mixed, receiver{&result});
    ex::start(op);
    EXPECT_EQ(result.stopped, 1);
    EXPECT_EQ(result.values + result.errors, 0);
}

TEST(ExecutionForwardingAttrs, ChainReportsOnlyWhatTheSenderBelowReportsDirectly) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();

    // A sender derived from an adaptor is not described by its base's edges.
    using base_t = decltype(ex::then(domain_source<passive_domain>{a}, value_callback{}));
    const auto derived = derived_adaptor<base_t>{
        ex::then(domain_source<passive_domain>{a}, value_callback{}), b};
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(ex::get_env(derived), ex::env<>{})),
        derived_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(ex::get_env(derived), ex::env<>{}), b);
    const auto above = ex::into_variant(derived);
    const auto above_attrs = ex::get_env(above);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(above_attrs, ex::env<>{})),
        derived_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(above_attrs, ex::env<>{}), b);

    // get_env of this sender is a prvalue that is not a scheduler, so the
    // public CPOs report nothing for it. The copy that an adaptor stores is
    // queried as a const lvalue, which is a scheduler; the scheduler-operand
    // rules of the CPOs must not be applied to it.
    using origin = prvalue_scheduler_attrs_source;
    static_assert(!ex::scheduler<lvalue_scheduler>);
    static_assert(ex::scheduler<const lvalue_scheduler&>);
    static_assert(!direct_scheduler<V, origin, ex::env<>>);
    static_assert(!direct_domain<V, origin, ex::env<>>);
    static_assert(no_completion_answers<ex::env_of_t<decltype(ex::then(origin{}, value_callback{}))>,
        ex::env<>>);
    static_assert(no_completion_answers<ex::env_of_t<decltype(ex::write_env(
        ex::stopped_as_error(origin{}, 1), ex::env<>{}))>, ex::env<>>);
}

namespace {

struct no_such_member_tag {};

// A value-only sender with a value scheduler and a value domain. Its error
// and stopped queries exist, but forming their result types is an error.
struct value_only_strict_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler scheduler;

    struct attributes {
        loop_scheduler scheduler;

        auto query(ex::get_completion_scheduler_t<V>) const noexcept { return scheduler; }
        value_site_domain query(ex::get_completion_domain_t<V>) const noexcept { return {}; }

        template<class Tag>
            requires (!std::same_as<Tag, V>) && (!std::same_as<Tag, void>)
        auto query(ex::get_completion_domain_t<Tag>) const noexcept {
            return typename Tag::this_member_does_not_exist{};
        }
        template<class Tag>
            requires (!std::same_as<Tag, V>)
        auto query(ex::get_completion_scheduler_t<Tag>) const noexcept {
            return typename Tag::this_member_does_not_exist{};
        }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<V(int)> {
        return {};
    }
    auto get_env() const noexcept -> attributes { return {scheduler}; }

    template<ex::receiver R>
    auto connect(R r) const {
        return ex::connect(ex::then(ex::schedule(scheduler), [] noexcept { return 1; }),
            std::move(r));
    }
};

} // namespace

TEST(ExecutionForwardingAttrs, ValueQueryDoesNotAskTheSourceAboutOtherChannels) {
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();
    const auto source = value_only_strict_source{scheduler};
    // A value query asks the source for its value answers only, even where
    // the edges would let another channel deliver the value: that channel is
    // looked at only if the source's signatures contain it.
    auto check = [&](const auto& sender) {
        const auto attrs = ex::get_env(sender);
        static_assert(std::same_as<decltype(ex::get_completion_domain<V>(attrs, ex::env<>{})),
            value_site_domain>);
        EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), scheduler);
    };
    check(ex::then(source, value_callback{}));
    check(ex::upon_error(source, value_callback{}));
    check(ex::upon_stopped(source, value_callback{}));
    check(ex::stopped_as_optional(source));
    check(ex::stopped_as_error(source, 1));
    check(ex::write_env(ex::into_variant(ex::upon_error(
        test_bulk::bulk(source, 2, bulk_callback{}), value_callback{})), ex::env<>{}));
    check(ex::schedule_from(source));

    // The sender connects and completes on the loop it names.
    result_state result;
    auto op = ex::connect(ex::upon_error(source, value_callback{}), receiver{&result});
    ex::start(op);
    loop.finish();
    loop.run();
    EXPECT_EQ(result.values, 1);
}

namespace {

// Domains whose COMMON-DOMAIN is not monotone in the set of sites:
// alone a is a, with b it is x, with c it is y, and with both it is a again.
struct domain_a {};
struct domain_b {};
struct domain_c {};
struct domain_x {};
struct domain_y {};

struct three_domain_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        domain_b query(ex::get_completion_domain_t<V>) const noexcept { return {}; }
        domain_a query(ex::get_completion_domain_t<E>) const noexcept { return {}; }
        domain_c query(ex::get_completion_domain_t<S>) const noexcept { return {}; }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<V(int), E(int), S()> { return {}; }
    auto get_env() const noexcept -> attributes { return {}; }
};

} // namespace

template<> struct std::common_type<domain_b, domain_a> { using type = domain_x; };
template<> struct std::common_type<domain_a, domain_b> { using type = domain_x; };
template<> struct std::common_type<domain_a, domain_c> { using type = domain_y; };
template<> struct std::common_type<domain_c, domain_a> { using type = domain_y; };
template<> struct std::common_type<domain_x, domain_c> { using type = domain_a; };
template<> struct std::common_type<domain_c, domain_x> { using type = domain_a; };

TEST(ExecutionForwardingAttrs, DomainNeedsEveryCombinationOfThrowingAdaptorsToAgree) {
    using source = three_domain_source;
    using attrs = ex::env_of_t<source>;
    // The sites of the error below: the error site alone if no callable
    // throws, with the value site if then's does, with the stopped site if
    // upon_stopped's does, and all three if both do.
    static_assert(std::same_as<ex::__forge_domain::__common_domain_t<domain_a>, domain_a>);
    static_assert(std::same_as<ex::__forge_domain::__common_domain_t<domain_b, domain_a>, domain_x>);
    static_assert(std::same_as<ex::__forge_domain::__common_domain_t<domain_a, domain_c>, domain_y>);
    static_assert(std::same_as<
        ex::__forge_domain::__common_domain_t<domain_b, domain_a, domain_c>, domain_a>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<E>(
        std::declval<const attrs&>(), ex::env<>{})), domain_a>);
    // Neither callable throwing and both throwing give domain_a, but one
    // throwing alone gives another domain, so the error has no answer.
    using nested = ex::env_of_t<decltype(ex::upon_stopped(
        ex::then(source{}, throwing_callback{}), value_callback{}))>;
    static_assert(no_answer<E, nested, ex::env<>>);
    // Each adaptor alone has two outcomes that differ.
    using single = ex::env_of_t<decltype(ex::then(source{}, throwing_callback{}))>;
    static_assert(no_answer<E, single, ex::env<>>);
    // Channels whose sites do not depend on a callable keep their answer:
    // the value of the outer recovery always has the value and stopped sites.
    static_assert(std::same_as<ex::__forge_domain::__common_domain_t<domain_b, domain_c>,
        ex::indeterminate_domain<domain_b, domain_c>>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<V>(
        std::declval<const nested&>(), ex::env<>{})),
        ex::indeterminate_domain<domain_b, domain_c>>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<S>(
        std::declval<const single&>(), ex::env<>{})), domain_c>);
}

TEST(ExecutionForwardingAttrs, IntoVariantInPlaceConstructionAvoidsDraftTupleMove) {
    using item = copy_nothrow_move_throwing;
    using tuple_type = std::tuple<item>;
    using variant_type = std::variant<tuple_type>;
    static_assert(std::is_nothrow_constructible_v<tuple_type, const item&>);
    static_assert(!std::is_nothrow_constructible_v<variant_type, tuple_type>);

    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto sender = ex::into_variant(site_source<ex::completion_signatures<V(const item&)>>{{a, b}});
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), ex::env<>>,
        ex::completion_signatures<V(variant_type)>>);
    const auto attrs = ex::get_env(sender);
    static_assert(no_answer<E, decltype(attrs), ex::env<>>);
    EXPECT_EQ(ex::get_completion_scheduler<V>(attrs, ex::env<>{}), a);

    const item value;
    auto reference = ex::then(ex::just(std::cref(value)),
        [](const auto& ref) noexcept -> const item& { return ref.get(); });
    result_state result;
    auto op = ex::connect(ex::into_variant(std::move(reference)), receiver{&result});
    ex::start(op);
    EXPECT_EQ(result.values, 1);
    EXPECT_EQ(result.errors, 0);
    EXPECT_EQ(result.stopped, 0);
}

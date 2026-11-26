#include <gtest/gtest.h>
#include <execution>
#include <concepts>
#include <exception>
#include <functional>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

constexpr auto empty_composed_environment() -> std::execution::env<> {
    return {};
}

static_assert(std::same_as<decltype(empty_composed_environment()),
                           std::execution::env<>>);

struct scheduler_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::inline_scheduler expected;
    bool* got_expected;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *got_expected = (sch == expected);
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::execution::get_scheduler_t{}, expected)};
    }
};

struct start_scheduler_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::inline_scheduler expected;
    bool* got_expected;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *got_expected = (sch == expected);
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::execution::get_start_scheduler_t{}, expected)};
    }
};

struct delegation_scheduler_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::inline_scheduler expected;
    bool* got_expected;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *got_expected = (sch == expected);
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::execution::get_delegation_scheduler_t{}, expected)};
    }
};

struct stop_token_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::inplace_stop_source* source;
    bool* stop_possible;
    bool* completed;

    void set_value(std::inplace_stop_token token) && noexcept {
        *stop_possible = token.stop_possible();
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(
                std::get_stop_token_t{}, source->get_token())};
    }
};

struct member_forwarding_query {
    constexpr int operator()(const std::execution::env<>&) const noexcept { return 42; }
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};

struct ref_qualified_forwarding_query : member_forwarding_query {
    constexpr bool query(std::forwarding_query_t) & noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) && noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) const& noexcept { return true; }
};

struct tag_forwarding_query {
    constexpr int operator()(const std::execution::env<>&) const noexcept { return 42; }
    friend constexpr bool tag_invoke(std::forwarding_query_t, tag_forwarding_query) noexcept {
        return true;
    }
};

struct await_completion_adaptor_env {
    int value = 0;

    int query(std::execution::get_await_completion_adaptor_t) const noexcept {
        return this->value;
    }
};

struct member_stop_query_env {
    std::inplace_stop_token token;

    auto query(std::get_stop_token_t) const noexcept
        -> std::inplace_stop_token {
        return token;
    }
};

struct overlay_value_query_t : std::forwarding_query_t {
    template<class Env>
        requires std::execution::__forge_env_detail::__queryable<
            overlay_value_query_t,
            Env>
    decltype(auto) operator()(Env&& env) const noexcept(
        std::execution::__forge_env_detail::__nothrow_query<
            overlay_value_query_t,
            Env>) {
        return std::execution::__forge_env_detail::__query(
            *this,
            static_cast<Env&&>(env));
    }
};

inline constexpr overlay_value_query_t overlay_value_query{};

struct throwing_copy_env {
    int value = 0;

    throwing_copy_env() = default;
    explicit throwing_copy_env(int initial) noexcept : value(initial) {}
    throwing_copy_env(throwing_copy_env&&) noexcept = default;
    throwing_copy_env& operator=(throwing_copy_env&&) noexcept = default;

    throwing_copy_env(const throwing_copy_env&) {
        throw std::runtime_error("overlay copied");
    }

    throwing_copy_env& operator=(const throwing_copy_env&) = delete;

    int query(overlay_value_query_t) const noexcept {
        return this->value;
    }
};

struct move_only_property_value {
    int value;

    explicit move_only_property_value(int initial) noexcept : value(initial) {}
    move_only_property_value(move_only_property_value&&) noexcept = default;
    move_only_property_value(const move_only_property_value&) = delete;
};

struct member_reference_env_holder {
    throwing_copy_env env{42};

    auto get_env() const noexcept -> const throwing_copy_env& {
        return env;
    }
};

struct tag_reference_env_holder {
    struct env_type : throwing_copy_env {
        std::inplace_stop_token token;

        explicit env_type(std::inplace_stop_token stop_token) noexcept
            : throwing_copy_env(53), token(stop_token) {}

        using throwing_copy_env::query;
        std::inplace_stop_token query(std::get_stop_token_t) const noexcept { return token; }
    };

    env_type env;

    friend auto tag_invoke(
        std::execution::get_env_t,
        const tag_reference_env_holder& self) noexcept
        -> const env_type& {
        return self.env;
    }
};

struct borrowed_attrs_key : std::forwarding_query_t {};
struct sender_local_key {};

struct borrowed_sender_attrs {
    std::execution::inline_scheduler scheduler;

    borrowed_sender_attrs() = default;
    borrowed_sender_attrs(const borrowed_sender_attrs&) = delete;
    auto operator=(const borrowed_sender_attrs&) -> borrowed_sender_attrs& = delete;

    const borrowed_sender_attrs& query(borrowed_attrs_key) const noexcept { return *this; }
    int query(sender_local_key) const noexcept { return 42; }

    auto query(std::execution::get_completion_scheduler_t<
                   std::execution::set_value_t>) const noexcept {
        return scheduler;
    }
};

struct borrowed_attrs_sender {
    using sender_concept = std::execution::sender_tag;
    const borrowed_sender_attrs* attributes;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> const borrowed_sender_attrs& {
        return *attributes;
    }

    template<std::execution::receiver R>
    auto connect(R receiver) const {
        return std::execution::connect(std::execution::just(42), std::move(receiver));
    }
};

template<class Attrs, class Q>
concept has_sender_attribute = requires(const Attrs& attrs, Q q) { attrs.query(q); };

} // namespace

TEST(WriteEnvTest, PreservesBorrowedNoncopyableSenderAttributes) {
    borrowed_sender_attrs attributes;
    const auto child = borrowed_attrs_sender{&attributes};
    static_assert(std::same_as<std::execution::env_of_t<decltype(child)>,
                               const borrowed_sender_attrs&>);
    auto sender = std::execution::write_env(child, std::execution::env<>{});
    static_assert(noexcept(std::execution::get_env(sender)));
    auto observed = std::execution::get_env(sender);
    static_assert(std::copy_constructible<decltype(observed)>);
    static_assert(!has_sender_attribute<decltype(observed), sender_local_key>);
    static_assert(std::same_as<decltype(observed.query(borrowed_attrs_key{})),
                               const borrowed_sender_attrs&>);
    const auto copy = observed;
    EXPECT_EQ(&observed.query(borrowed_attrs_key{}), &attributes);
    EXPECT_EQ(&copy.query(borrowed_attrs_key{}), &attributes);
    EXPECT_EQ(std::execution::get_completion_scheduler<std::execution::set_value_t>(
                  std::execution::get_env(child)), attributes.scheduler);
    EXPECT_EQ(std::execution::get_completion_scheduler<std::execution::set_value_t>(
                  observed, std::execution::env<>{}), attributes.scheduler);
    auto result = std::this_thread::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionWriteEnvTest, EmptyEnvSupportsCopyListInitialization) {
    std::execution::env<> environment = {};
    auto returned = empty_composed_environment();
    static_assert(std::same_as<decltype(environment), decltype(returned)>);
    EXPECT_FALSE(std::get_stop_token(environment).stop_possible());
    EXPECT_FALSE(std::get_stop_token(returned).stop_possible());
}

template<class Query>
concept has_forwarding_friend = requires(const Query& q) { tag_invoke(std::forwarding_query, q); };

template<class Query>
constexpr bool has_only_forwarding_member(const Query& q) {
    static_assert(!has_forwarding_friend<Query>);
    return q.query(std::forwarding_query) && std::forwarding_query(q);
}

static_assert(has_only_forwarding_member(std::execution::get_scheduler));
static_assert(has_only_forwarding_member(std::execution::get_start_scheduler));
static_assert(has_only_forwarding_member(std::execution::get_delegation_scheduler));
static_assert(has_only_forwarding_member(std::execution::get_forward_progress_guarantee));
static_assert(has_only_forwarding_member(std::get_stop_token));
static_assert(has_only_forwarding_member(std::get_allocator));
static_assert(has_only_forwarding_member(std::execution::get_completion_scheduler<std::execution::set_value_t>));
static_assert(has_only_forwarding_member(std::execution::get_domain));
static_assert(has_only_forwarding_member(std::execution::get_completion_domain<std::execution::set_value_t>));
static_assert(has_only_forwarding_member(std::execution::get_await_completion_adaptor));
static_assert(std::forwarding_query(member_forwarding_query{}));
static_assert(!std::forwarding_query(tag_forwarding_query{}));
static_assert(!std::forwarding_query(std::execution::set_value));

TEST(WriteEnvTest, ForwardingQueryUsesOriginalConstLvalue) {
    const ref_qualified_forwarding_query query;
    EXPECT_TRUE(std::forwarding_query(query));
}

TEST(WriteEnvTest, PropQueriesBorrowMoveOnlyValuesWithoutCopying) {
    auto property = std::execution::prop{
        overlay_value_query_t{},
        move_only_property_value{42}};

    static_assert(std::same_as<
        decltype(overlay_value_query(property)),
        const move_only_property_value&>);
    static_assert(noexcept(overlay_value_query(property)));
    EXPECT_EQ(overlay_value_query(property).value, 42);
}

TEST(EnvTest, ReferenceWrapperBorrowsMemberQueryEnvironment) {
    std::inplace_stop_source source;
    member_stop_query_env parent{source.get_token()};
    auto env = std::execution::env{std::ref(parent)};
    static_assert(std::same_as<decltype(env),
                              std::execution::env<member_stop_query_env&>>);
    EXPECT_EQ(std::get_stop_token(env), source.get_token());

    parent.token = {};
    EXPECT_FALSE(std::get_stop_token(env).stop_possible());

    const member_stop_query_env const_parent{source.get_token()};
    auto const_env = std::execution::env{std::cref(const_parent)};
    static_assert(std::same_as<decltype(const_env),
        std::execution::env<const member_stop_query_env&>>);
    EXPECT_EQ(std::get_stop_token(const_env), source.get_token());
}

TEST(WriteEnvTest, GetEnvPreservesMemberReferenceWithoutCopying) {
    member_reference_env_holder holder;
    static_assert(std::same_as<
        std::execution::env_of_t<member_reference_env_holder>,
        const throwing_copy_env&>);
    static_assert(noexcept(std::execution::get_env(holder)));
    const auto& env = std::execution::get_env(holder);
    EXPECT_EQ(&env, &holder.env);
    EXPECT_EQ(overlay_value_query(env), 42);
}

TEST(WriteEnvTest, GetEnvUsesEmptyEnvironmentForTagOnlyObject) {
    std::inplace_stop_source source;
    ASSERT_TRUE(source.request_stop());
    tag_reference_env_holder holder{tag_reference_env_holder::env_type(source.get_token())};
    EXPECT_EQ(overlay_value_query(holder.env), 53);
    EXPECT_TRUE(std::get_stop_token(holder.env).stop_requested());
    // [exec.get.env]/1.2 ignores the ADL overload, even when it returns a reference.
    static_assert(std::same_as<std::execution::env_of_t<tag_reference_env_holder>,
                               std::execution::env<>>);
    static_assert(noexcept(std::execution::get_env(holder)));
    EXPECT_FALSE(std::get_stop_token(std::execution::get_env(holder)).stop_possible());
}

TEST(WriteEnvTest, PropUnwrapsReferenceWrappers) {
    int value = 42;
    auto property = std::execution::prop{
        overlay_value_query_t{},
        std::ref(value)};

    static_assert(std::same_as<
        decltype(overlay_value_query(property)),
        int&>);
    EXPECT_EQ(&overlay_value_query(property), &value);
}

TEST(WriteEnvTest, InjectedEnvOverridesReceiverEnv) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::env{
        std::execution::prop(std::execution::get_scheduler_t{}, injected)};

    auto sndr = std::execution::write_env(
        std::execution::read_env(std::execution::get_scheduler), env);
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::env<>{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::execution::inline_scheduler)>>);

    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, DoesNotCopyOverlayWhileQueryingNestedAdaptorEnv) {
    auto sender = std::execution::write_env(
        std::execution::read_env(overlay_value_query)
            | std::execution::then([](int value) noexcept { return value; }),
        throwing_copy_env{42});

    auto result = std::this_thread::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(WriteEnvTest, InjectsSchedulerProperty) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::env{
        std::execution::prop(std::execution::get_scheduler_t{}, injected)};

    auto result = std::this_thread::sync_wait(std::execution::write_env(
        std::execution::read_env(std::execution::get_scheduler), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, ReadsInjectedStartScheduler) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::env{
        std::execution::prop(std::execution::get_start_scheduler_t{}, injected)};

    auto result = std::this_thread::sync_wait(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_start_scheduler),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, ReadsInjectedDelegationScheduler) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::env{
        std::execution::prop(std::execution::get_delegation_scheduler_t{}, injected)};

    auto result = std::this_thread::sync_wait(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_delegation_scheduler),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(SyncWaitEnvTest, ExposesTheSameRunLoopSchedulerForAllRoles) {
    auto result = std::this_thread::sync_wait(
        std::execution::when_all(
            std::execution::read_env(std::execution::get_scheduler),
            std::execution::read_env(std::execution::get_start_scheduler),
            std::execution::read_env(std::execution::get_delegation_scheduler))
        | std::execution::then([](auto scheduler, auto start, auto delegation) {
              return scheduler == start && scheduler == delegation;
          }));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
}

TEST(WriteEnvTest, FallsBackToReceiverEnvForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_scheduler),
            std::execution::env<>{}),
        scheduler_receiver{downstream, &got_expected, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_expected);
}

TEST(WriteEnvTest, FallsBackToReceiverStartSchedulerForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_start_scheduler),
            std::execution::env<>{}),
        start_scheduler_receiver{downstream, &got_expected, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_expected);
}

TEST(WriteEnvTest, FallsBackToReceiverDelegationSchedulerForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_delegation_scheduler),
            std::execution::env<>{}),
        delegation_scheduler_receiver{downstream, &got_expected, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_expected);
}

TEST(WriteEnvTest, PreservesReceiverStopTokenWhenNotOverridden) {
    std::inplace_stop_source source;
    bool stop_possible = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::get_stop_token),
            std::execution::env<>{}),
        stop_token_receiver{&source, &stop_possible, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(stop_possible);
}

TEST(WriteEnvTest, ReadsMemberQueriedStopTokenFromInjectedEnv) {
    std::inplace_stop_source source;
    EXPECT_TRUE(source.request_stop());

    auto result = std::this_thread::sync_wait(std::execution::write_env(
        std::execution::read_env(std::get_stop_token),
        member_stop_query_env{source.get_token()}));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result).stop_requested());
}

TEST(EnvTest, DuplicateQueriesUseTheLeftmostEnvironment) {
    std::inplace_stop_source first;
    std::inplace_stop_source second;
    auto env = std::execution::env{
        std::execution::prop(
            std::get_stop_token_t{}, first.get_token()),
        std::execution::prop(
            std::get_stop_token_t{}, second.get_token())};

    auto token = std::get_stop_token(env);
    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());

    second.request_stop();
    EXPECT_FALSE(token.stop_requested());

    first.request_stop();
    EXPECT_TRUE(token.stop_requested());
}

TEST(QueryCpoTest, ReadsAwaitCompletionAdaptorThroughBackportQueryModel) {
    await_completion_adaptor_env env{42};

    EXPECT_EQ(std::execution::get_await_completion_adaptor(env), 42);
}

TEST(UnstoppableTest, OverridesReceiverStopTokenWithNeverStopToken) {
    std::inplace_stop_source source;
    source.request_stop();
    auto env = std::execution::env{
        std::execution::prop(
            std::get_stop_token_t{}, source.get_token())};

    auto sndr = std::execution::write_env(
        std::execution::unstoppable(
            std::execution::read_env(std::get_stop_token)),
        env);

    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::env<>{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(const std::never_stop_token&)>>);

    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    auto token = std::get<0>(*result);
    static_assert(std::unstoppable_token<decltype(token)>);
    EXPECT_FALSE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());
}

TEST(UnstoppableTest, OverridesStopTokenInsideWriteEnv) {
    std::inplace_stop_source source;
    auto env = std::execution::env{
        std::execution::prop(
            std::get_stop_token_t{}, source.get_token())};

    auto result = std::this_thread::sync_wait(
        std::execution::write_env(
            std::execution::unstoppable(
                std::execution::read_env(std::get_stop_token)),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(std::get<0>(*result).stop_possible());
}

namespace {

namespace ex = std::execution;
using value_tag = ex::set_value_t;
using error_tag = ex::set_error_t;
using stopped_tag = ex::set_stopped_t;

template<class Tag, class Attrs, class... Envs>
concept has_site_scheduler = requires(const Attrs& attrs, const Envs&... envs) {
    ex::get_completion_scheduler<Tag>(attrs, envs...);
};

template<class Tag, class Attrs, class... Envs>
concept has_site_domain = requires(const Attrs& attrs, const Envs&... envs) {
    ex::get_completion_domain<Tag>(attrs, envs...);
};

struct overlay_domain {};
struct base_domain {};
struct local_scheduler_key {};
using loop_scheduler = decltype(std::declval<ex::run_loop&>().get_scheduler());

struct local_scheduler_env {
    loop_scheduler scheduler;
    auto query(local_scheduler_key) const noexcept { return scheduler; }
};

struct local_routing_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const local_routing_scheduler&) const noexcept = default;

    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }

    template<class Env>
        requires requires(const Env& e) { e.query(local_scheduler_key{}); }
    auto query(ex::get_completion_scheduler_t<value_tag>, const Env& e) const noexcept {
        return e.query(local_scheduler_key{});
    }
};

struct local_routing_source {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<value_tag()> { return {}; }

    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<value_tag>, local_routing_scheduler{}};
    }
};

struct route_to_error : std::forwarding_query_t {};

// Completes with an error when the environment carries the route, and with a
// value otherwise.
struct overlay_switch_source {
    using sender_concept = ex::sender_tag;
    loop_scheduler value_scheduler;
    loop_scheduler error_scheduler;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (requires(const Env& env) { env.query(route_to_error{}); }) {
            return ex::completion_signatures<error_tag(int)>{};
        } else {
            return ex::completion_signatures<value_tag(int)>{};
        }
    }

    auto get_env() const noexcept {
        return ex::env{
            ex::prop{ex::get_completion_scheduler<value_tag>, value_scheduler},
            ex::prop{ex::get_completion_scheduler<error_tag>, error_scheduler},
            ex::prop{ex::get_completion_domain<value_tag>, base_domain{}},
            ex::prop{ex::get_completion_domain<error_tag>, overlay_domain{}}};
    }

    template<class R>
    struct routed_receiver {
        using receiver_concept = ex::receiver_tag;
        R outer;
        bool error;

        void set_value() && noexcept {
            if (error) {
                ex::set_error(std::move(outer), 7);
            } else {
                ex::set_value(std::move(outer), 7);
            }
        }
        void set_stopped() && noexcept { std::terminate(); }
        auto get_env() const noexcept -> ex::env<> { return {}; }
    };

    template<ex::receiver R>
    auto connect(R r) const {
        constexpr bool error = requires(const ex::env_of_t<R>& env) { env.query(route_to_error{}); };
        return ex::connect(ex::schedule(error ? error_scheduler : value_scheduler),
            routed_receiver<R>{std::move(r), error});
    }
};

// Always completes with a value. The environment selects where.
struct routed_value_source {
    using sender_concept = ex::sender_tag;

    struct attributes {
        loop_scheduler plain;
        loop_scheduler routed;

        template<class Env>
        auto query(ex::get_completion_scheduler_t<value_tag>, const Env& env) const noexcept {
            if constexpr (requires { env.query(route_to_error{}); }) {
                return routed;
            } else {
                return plain;
            }
        }

        template<class Env>
        auto query(ex::get_completion_domain_t<value_tag>, const Env& env) const noexcept {
            if constexpr (requires { env.query(route_to_error{}); }) {
                return overlay_domain{};
            } else {
                return base_domain{};
            }
        }
    };

    attributes attrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<value_tag(int)> { return {}; }
    auto get_env() const noexcept { return attrs; }
};

struct counting_receiver {
    using receiver_concept = ex::receiver_tag;
    int* values;

    template<class... Args>
    void set_value(Args&&...) && noexcept { ++*values; }
    template<class Error>
    void set_error(Error&&) && noexcept { std::terminate(); }
    void set_stopped() && noexcept { std::terminate(); }
    auto get_env() const noexcept -> ex::env<> { return {}; }
};

// An allocator-aware overlay: connection rebuilds it with the allocator of
// the receiver, and the rebuilt overlay names another scheduler.
struct allocator_overlay {
    using allocator_type = std::allocator<std::byte>;

    loop_scheduler scheduler;
    loop_scheduler rebuilt;

    allocator_overlay(loop_scheduler s, loop_scheduler r) noexcept : scheduler(s), rebuilt(r) {}
    allocator_overlay(const allocator_overlay&) = default;
    allocator_overlay(std::allocator_arg_t, const allocator_type&,
                      const allocator_overlay& other) noexcept
        : scheduler(other.rebuilt), rebuilt(other.rebuilt) {}

    auto query(ex::get_scheduler_t) const noexcept { return scheduler; }
    overlay_domain query(ex::get_domain_t) const noexcept { return {}; }
};

struct scheduler_observer {
    using receiver_concept = ex::receiver_tag;
    loop_scheduler* observed;

    struct env {
        auto query(std::get_allocator_t) const noexcept { return std::allocator<std::byte>{}; }
    };

    void set_value(loop_scheduler scheduler) && noexcept { *observed = scheduler; }
    template<class Error>
    void set_error(Error&&) && noexcept { std::terminate(); }
    void set_stopped() && noexcept { std::terminate(); }
    auto get_env() const noexcept -> env { return {}; }
};

} // namespace

TEST(WriteEnvTest, ComputesInlineCompletionAttributesInOverlayFirstOrder) {
    ex::run_loop outer_loop, overlay_loop;
    const auto outer = outer_loop.get_scheduler();
    const auto overlay = overlay_loop.get_scheduler();
    const auto base = ex::env{ex::prop{ex::get_scheduler, outer},
                              ex::prop{ex::get_domain, base_domain{}}};
    const auto child = ex::schedule(ex::inline_scheduler{});
    const auto sender = ex::write_env(child,
        ex::env{ex::prop{ex::get_scheduler, overlay},
                ex::prop{ex::get_domain, overlay_domain{}}});
    const auto attrs = ex::get_env(sender);
    static_assert(!has_site_scheduler<value_tag, decltype(attrs)>);
    static_assert(!has_site_scheduler<error_tag, decltype(attrs), decltype(base)>);
    static_assert(!has_site_domain<error_tag, decltype(attrs), decltype(base)>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(attrs, base)),
        overlay_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(ex::get_env(child), base), outer);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, base), overlay);
}

TEST(WriteEnvTest, OverlayCanChangeTheValueSiteWithoutRemovingValue) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto child = routed_value_source{{a, b}};
    const auto sender = ex::write_env(child, ex::prop{route_to_error{}, true});
    const auto attrs = ex::get_env(sender);

    // The output tag is the same with and without the overlay; its site and
    // its domain are the ones that the child reports under the overlay.
    static_assert(!has_site_scheduler<value_tag, decltype(attrs)>);
    static_assert(!has_site_domain<value_tag, decltype(attrs)>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(
        ex::get_env(child), ex::env<>{})), base_domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(
        attrs, ex::env<>{})), overlay_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(ex::get_env(child), ex::env<>{}), a);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, ex::env<>{}), b);
    // An outer environment that already routes gives the same answer.
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, ex::prop{route_to_error{}, true}), b);
}

TEST(WriteEnvTest, OverlayCanMoveAValueToTheRecoveredErrorSite) {
    ex::run_loop first, second;
    const auto a = first.get_scheduler();
    const auto b = second.get_scheduler();
    const auto child = ex::upon_error(overlay_switch_source{a, b}, [](int n) noexcept { return n; });
    const auto sender = ex::write_env(child, ex::prop{route_to_error{}, true});
    const auto attrs = ex::get_env(sender);

    using signature = ex::completion_signatures<value_tag(int)>;
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(child), ex::env<>>, signature>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), ex::env<>>, signature>);
    // Without the overlay the value of the source passes through.
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(
        ex::get_env(child), ex::env<>{})), base_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(ex::get_env(child), ex::env<>{}), a);
    // Under the overlay the value is a recovered error, delivered at the
    // error site. The unary retry cannot bring the other site back.
    static_assert(!has_site_scheduler<value_tag, decltype(attrs)>);
    static_assert(!has_site_domain<value_tag, decltype(attrs)>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(
        attrs, ex::env<>{})), overlay_domain>);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, ex::env<>{}), b);

    int values = 0;
    auto op = ex::connect(sender, counting_receiver{&values});
    ex::start(op);
    first.finish();
    first.run();
    EXPECT_EQ(values, 0);
    second.finish();
    second.run();
    EXPECT_EQ(values, 1);
}

TEST(WriteEnvTest, WithholdsSchedulerOfAnOverlayThatConnectionCanRebuild) {
    ex::run_loop stored_loop, rebuilt_loop, outer_loop;
    const auto stored = stored_loop.get_scheduler();
    const auto rebuilt = rebuilt_loop.get_scheduler();
    const auto sender = ex::write_env(ex::read_env(ex::get_scheduler),
        allocator_overlay{stored, rebuilt});
    const auto inline_sender = ex::write_env(ex::schedule(ex::inline_scheduler{}),
        allocator_overlay{stored, rebuilt});
    const auto attrs = ex::get_env(inline_sender);

    // Without an allocator the stored overlay is the connected one.
    const ex::env<> plain;
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, plain), stored);
    // A receiver with an allocator makes the operation hold a rebuilt overlay.
    loop_scheduler seen = outer_loop.get_scheduler();
    auto op = ex::connect(sender, scheduler_observer{&seen});
    ex::start(op);
    EXPECT_EQ(seen, rebuilt);

    const scheduler_observer::env allocating;
    static_assert(!has_site_scheduler<value_tag, decltype(attrs), scheduler_observer::env>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<value_tag>(attrs, allocating)),
        overlay_domain>);
    // An overlay that is not allocator-aware keeps its scheduler.
    // The attributes point at the overlay that the sender owns.
    const auto fixed_sender = ex::write_env(ex::schedule(ex::inline_scheduler{}),
        ex::prop{ex::get_scheduler, stored});
    const auto fixed = ex::get_env(fixed_sender);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(fixed, allocating), stored);
}

TEST(WriteEnvTest, WithholdsSchedulerThatOuterRecursionWouldReroute) {
    ex::run_loop loop;
    const auto base = local_scheduler_env{loop.get_scheduler()};
    const auto child = local_routing_source{};
    const auto sender = ex::write_env(child, ex::env<>{});
    const auto attrs = ex::get_env(sender);

    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(ex::get_env(child), base), base.scheduler);
    static_assert(!has_site_scheduler<value_tag, decltype(attrs), decltype(base)>);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, ex::env<>{}), local_routing_scheduler{});
    static_assert(!has_site_domain<value_tag, decltype(attrs), decltype(base)>);
}

TEST(WriteEnvTest, UnstoppableRemovesAChildStoppedSite) {
    ex::run_loop loop;
    const auto scheduler = loop.get_scheduler();
    const auto child = ex::schedule(scheduler);
    const auto base = ex::prop{std::get_stop_token, std::inplace_stop_token{}};
    const auto passthrough = ex::write_env(child, ex::env<>{});
    const auto pass_attrs = ex::get_env(passthrough);
    const auto wrapped = ex::unstoppable(child);
    const auto attrs = ex::get_env(wrapped);

    static_assert(!has_site_scheduler<stopped_tag, decltype(attrs)>);
    static_assert(!has_site_scheduler<stopped_tag, decltype(attrs), decltype(base)>);
    static_assert(!has_site_domain<stopped_tag, decltype(attrs), decltype(base)>);
    static_assert(has_site_domain<stopped_tag, decltype(pass_attrs), decltype(base)>);
    EXPECT_EQ(ex::get_completion_scheduler<stopped_tag>(pass_attrs, base), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<value_tag>(attrs, base), scheduler);
}

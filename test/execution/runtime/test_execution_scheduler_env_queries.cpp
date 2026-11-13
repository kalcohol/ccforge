// Include fragments directly so native stand-aside cannot hide regressions.
#include <forge/execution_extensions.hpp>
#include "cpp26/execution/inline_scheduler.hpp"
#include "cpp26/execution/run_loop.hpp"
#include "cpp26/execution/write_env.hpp"
#include "cpp26/execution/counting_scope.hpp"
#include "cpp26/execution/spawn_future.hpp"
#include <gtest/gtest.h>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace ex = std::execution;
namespace {
struct forwarding_key {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
struct local_key {};
struct probe_env {
    int forwarded;
    int local;
    int query(forwarding_key) const noexcept { return forwarded; }
    int query(local_key) const noexcept { return local; }
};
template<class E>
concept has_local = requires(const E& e) { e.query(local_key{}); };

template<bool OverlayLocal>
struct probe_sender {
    using sender_concept = ex::sender_t;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        static_assert(has_local<Env> == OverlayLocal);
        return ex::completion_signatures<ex::set_value_t(int)>{};
    }
    template<class R>
    struct op {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        void start() & noexcept {
            auto e = ex::get_env(receiver);
            static_assert(has_local<decltype(e)> == OverlayLocal);
            int value = e.query(forwarding_key{});
            if constexpr (OverlayLocal) {
                value += e.query(local_key{});
            }
            ex::set_value(std::move(receiver), value);
        }
    };
    template<ex::receiver R>
    auto connect(R r) const noexcept -> op<R> { return {std::move(r)}; }
};
struct probe_receiver {
    using receiver_concept = ex::receiver_t;
    int* result;
    void set_value(int value) && noexcept { *result = value; }
    probe_env get_env() const noexcept { return {10, 20}; }
};

struct domain {};
struct domain_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_scheduler_t) const noexcept { return scheduler; }
    auto query(ex::get_domain_t) const noexcept -> domain { return {}; }
};

struct domain_only_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_start_scheduler_t) const noexcept { return scheduler; }
    auto query(ex::get_domain_t) const noexcept -> domain { return {}; }
};
struct missing_scheduler_receiver {
    using receiver_concept = ex::receiver_t;
    domain_only_env environment;
    bool* matched;
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
    void set_value(ex::run_loop::scheduler scheduler) && noexcept {
        *matched = scheduler == environment.scheduler;
    }
#else
    void set_value(ex::inline_scheduler scheduler) && noexcept {
        *matched = scheduler == ex::inline_scheduler{};
    }
#endif
    void set_error(std::exception_ptr) && noexcept { *matched = false; }
    domain_only_env get_env() const noexcept { return environment; }
};

template<class Sender, class Scheduler>
void check_schedule_attributes(const Sender& sender, const Scheduler& scheduler) {
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attrs), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler), scheduler);
}

struct stop_env {
    std::inplace_stop_token token;
    int* queries;
    auto query(ex::get_stop_token_t) const noexcept {
        ++*queries;
        return token;
    }
};
struct stop_receiver {
    using receiver_concept = ex::receiver_t;
    stop_env environment;
    bool* stopped;
    void set_value() && noexcept { *stopped = false; }
    void set_stopped() && noexcept { *stopped = true; }
    stop_env get_env() const noexcept { return environment; }
};

struct let_receiver_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_scheduler_t) const noexcept -> const ex::run_loop::scheduler& {
        return scheduler;
    }
    auto query(ex::get_start_scheduler_t) const noexcept { return ex::inline_scheduler{}; }
    int query(local_key) const noexcept { return 42; }
};

template<class Tag>
struct unary_let_attrs {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_completion_scheduler_t<Tag>) const noexcept
        -> const ex::run_loop::scheduler& { return scheduler; }
    // A scheduler answer must take precedence over a direct domain answer.
    auto query(ex::get_completion_domain_t<Tag>) const noexcept -> domain { return {}; }
};

template<class Tag>
struct dependent_let_attrs : unary_let_attrs<Tag> {
    using unary_let_attrs<Tag>::query;
    bool* filtered;
    int* queries;
    template<class Env>
    auto query(ex::get_completion_scheduler_t<Tag>, Env&& env) const
        noexcept(noexcept(ex::get_scheduler(static_cast<Env&&>(env))))
        -> decltype(ex::get_scheduler(static_cast<Env&&>(env))) {
        *filtered = !has_local<Env>;
        ++*queries;
        return ex::get_scheduler(static_cast<Env&&>(env));
    }
};

template<class Tag, class Attrs>
struct let_predecessor {
    using sender_concept = ex::sender_t;
    Attrs attrs;
    // Returning attributes by value exercises a query result referring into
    // the temporary attributes, which let must copy before returning.
    Attrs get_env() const noexcept { return attrs; }
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (std::same_as<Tag, ex::set_error_t>) {
            return ex::completion_signatures<ex::set_error_t(int)>{};
        } else {
            return ex::completion_signatures<Tag()>{};
        }
    }
    template<class R>
    struct op {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        void start() & noexcept {
            if constexpr (std::same_as<Tag, ex::set_error_t>) {
                ex::set_error(std::move(receiver), 7);
            } else {
                Tag{}(std::move(receiver));
            }
        }
    };
    template<ex::receiver R>
    auto connect(R r) const noexcept -> op<R> { return {std::move(r)}; }
};

template<class Tag, class Attrs>
auto make_scheduler_let(Attrs attrs) {
    auto child = [](auto&...) { return ex::read_env(ex::get_start_scheduler); };
    auto predecessor = let_predecessor<Tag, Attrs>{std::move(attrs)};
    if constexpr (std::same_as<Tag, ex::set_value_t>) {
        return ex::let_value(std::move(predecessor), child);
    } else if constexpr (std::same_as<Tag, ex::set_error_t>) {
        return ex::let_error(std::move(predecessor), child);
    } else {
        return ex::let_stopped(std::move(predecessor), child);
    }
}

template<bool Borrow>
struct let_receiver {
    using receiver_concept = ex::receiver_t;
    const let_receiver_env* environment;
    ex::run_loop::scheduler expected;
    bool* matched;
    void set_value(ex::run_loop::scheduler scheduler) && noexcept { *matched = scheduler == expected; }
    void set_error(std::exception_ptr) && noexcept { *matched = false; }
    void set_stopped() && noexcept { *matched = false; }
    decltype(auto) get_env() const noexcept {
        if constexpr (Borrow) return (*environment);
        else return let_receiver_env{*environment};
    }
};

template<bool Borrow, class Tag, class Attrs>
void check_let_scheduler(Attrs attrs, ex::run_loop::scheduler outer,
                         ex::run_loop::scheduler expected) {
    ex::run_loop changed_loop;
    let_receiver_env environment{outer};
    bool matched = false;
    auto sender = make_scheduler_let<Tag>(std::move(attrs));
    using env_t = ex::env_of_t<let_receiver<Borrow>>;
    static_assert(std::same_as<ex::value_types_of_t<decltype(sender), env_t>,
                               std::variant<std::tuple<ex::run_loop::scheduler>>>);
    auto operation = ex::connect(std::move(sender),
        let_receiver<Borrow>{&environment, expected, &matched});
    // A reference query answer must be captured as a value at connect time.
    // This also exercises references into a receiver environment returned by value.
    environment.scheduler = changed_loop.get_scheduler();
    ex::start(operation);
    EXPECT_TRUE(matched);
}

template<class Tag>
void check_unary_let_scheduler() {
    ex::run_loop predecessor_loop;
    ex::run_loop receiver_loop;
    auto attrs = unary_let_attrs<Tag>{predecessor_loop.get_scheduler()};
    check_let_scheduler<false, Tag>(attrs, receiver_loop.get_scheduler(), attrs.scheduler);
    check_let_scheduler<true, Tag>(attrs, receiver_loop.get_scheduler(), attrs.scheduler);
}

template<class Tag>
void check_dependent_let_scheduler() {
    ex::run_loop predecessor_loop;
    ex::run_loop receiver_loop;
    bool filtered = false;
    int queries = 0;
    auto attrs = dependent_let_attrs<Tag>{{predecessor_loop.get_scheduler()}, &filtered, &queries};
    check_let_scheduler<false, Tag>(attrs, receiver_loop.get_scheduler(), receiver_loop.get_scheduler());
    EXPECT_TRUE(filtered);
    EXPECT_EQ(queries, 1);
    filtered = false;
    check_let_scheduler<true, Tag>(attrs, receiver_loop.get_scheduler(), receiver_loop.get_scheduler());
    EXPECT_TRUE(filtered);
    EXPECT_EQ(queries, 2);
}
} // namespace

TEST(SchedulerEnvQueriesTest, WriteEnvFiltersLocalBaseAndKeepsLocalOverlay) {
    int value = -1;
    auto base_sender = ex::write_env(probe_sender<false>{}, forge::empty_env{});
    using base_signatures = ex::completion_signatures_of_t<decltype(base_sender), probe_env>;
    static_assert(std::same_as<base_signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    auto base = ex::connect(std::move(base_sender), probe_receiver{&value});
    ex::start(base);
    EXPECT_EQ(value, 10);

    auto overlay_sender = ex::write_env(probe_sender<true>{}, probe_env{30, 40});
    using overlay_signatures = ex::completion_signatures_of_t<decltype(overlay_sender), probe_env>;
    static_assert(std::same_as<overlay_signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    auto overlay = ex::connect(std::move(overlay_sender), probe_receiver{&value});
    ex::start(overlay);
    EXPECT_EQ(value, 70);
}

TEST(SchedulerEnvQueriesTest, InlineSchedulerUsesTheSuppliedResourceAndDomain) {
    ex::run_loop first;
    ex::run_loop second;
    ex::inline_scheduler scheduler;
    auto attrs = ex::get_env(ex::schedule(scheduler));
    domain_env environment{first.get_scheduler()};
    EXPECT_EQ(scheduler.query(ex::get_completion_scheduler<ex::set_value_t>, environment),
              first.get_scheduler());
    EXPECT_EQ(attrs.query(ex::get_completion_scheduler<ex::set_value_t>, environment),
              first.get_scheduler());
    environment.scheduler = second.get_scheduler();
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, environment),
              second.get_scheduler());
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attrs, environment),
              second.get_scheduler());
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                                   scheduler, environment)), domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                                   attrs, environment)), domain>);
    (void)attrs.query(ex::get_completion_domain<ex::set_value_t>, environment);
    (void)scheduler.query(ex::get_completion_domain<ex::set_value_t>, environment);
}

TEST(SchedulerEnvQueriesTest, RunLoopSchedulerAndAttributesHaveValueAndStoppedParity) {
    ex::run_loop first;
    ex::run_loop second;
    auto scheduler = first.get_scheduler();
    auto sender = ex::schedule(scheduler);
    check_schedule_attributes(sender, scheduler);
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(scheduler), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(attrs), scheduler);
    EXPECT_NE(ex::get_completion_scheduler<ex::set_value_t>(scheduler), second.get_scheduler());
    // TRY-QUERY falls back to unary queries when environment arguments are supplied.
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, forge::empty_env{}),
              ex::get_completion_scheduler<ex::set_value_t>(attrs, forge::empty_env{}));
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(scheduler, forge::empty_env{}),
              ex::get_completion_scheduler<ex::set_stopped_t>(attrs, forge::empty_env{}));
}

TEST(SchedulerEnvQueriesTest, InlineScheduleLetAndWriteEnvReachTheChildEnvironment) {
    ex::run_loop loop;
    auto scheduler = loop.get_scheduler();
    auto sender = ex::write_env(
        ex::schedule(ex::inline_scheduler{}) | ex::let_value([] {
            return ex::read_env(ex::get_scheduler);
        }), ex::prop{ex::get_scheduler, scheduler});
    auto result = forge::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), scheduler);
}

TEST(SchedulerEnvQueriesTest, InlineLetWithoutReceiverSchedulerUsesTheModeFallback) {
    ex::run_loop loop;
    domain_only_env environment{loop.get_scheduler()};
    auto predecessor = ex::schedule(ex::inline_scheduler{});
    auto selected = ex::__forge_let::__make_let_env<ex::__forge_let::__value_tag>(
        predecessor, environment);
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
    // Missing get_scheduler selects the domain alternative, not SCHED-ENV.
    static_assert(std::same_as<decltype(selected), ex::prop<ex::get_domain_t, domain>>);
    static_assert(std::same_as<decltype(ex::get_domain(selected)), domain>);
#else
    static_assert(std::same_as<decltype(selected),
                               ex::__forge_let::__scheduler_env<ex::inline_scheduler>>);
    EXPECT_EQ(ex::get_start_scheduler(selected), ex::inline_scheduler{});
#endif
    auto sender = ex::let_value(predecessor, [] {
        return ex::read_env(ex::get_start_scheduler);
    });
    bool matched = false;
    auto operation = ex::connect(std::move(sender), missing_scheduler_receiver{environment, &matched});
    ex::start(operation);
    EXPECT_TRUE(matched);
}

TEST(SchedulerEnvQueriesTest, LetValueKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_value_t>();
}
TEST(SchedulerEnvQueriesTest, LetErrorKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_error_t>();
}
TEST(SchedulerEnvQueriesTest, LetStoppedKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_stopped_t>();
}
TEST(SchedulerEnvQueriesTest, LetValueUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_value_t>();
}
TEST(SchedulerEnvQueriesTest, LetErrorUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_error_t>();
}
TEST(SchedulerEnvQueriesTest, LetStoppedUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_stopped_t>();
}

TEST(SchedulerEnvQueriesTest, RunLoopScheduleReadsTheReceiverStopEnvironment) {
    ex::run_loop loop;
    std::inplace_stop_source source;
    source.request_stop();
    int queries = 0;
    bool stopped = false;
    auto operation = ex::connect(ex::write_env(ex::schedule(loop.get_scheduler()), forge::empty_env{}),
        stop_receiver{{source.get_token(), &queries}, &stopped});
    ex::start(operation);
    loop.finish();
    loop.run();
    EXPECT_TRUE(stopped);
    EXPECT_GT(queries, 0);
}

TEST(SchedulerEnvQueriesTest, RunLoopScheduleLetAndWriteEnvReachTheChildEnvironment) {
    ex::run_loop loop;
    auto scheduler = loop.get_scheduler();
    auto sender = ex::write_env(ex::schedule(scheduler) | ex::let_value([] {
        return ex::read_env(ex::get_scheduler);
    }), ex::prop{ex::get_scheduler, scheduler});
    // Queue before run(); finish drains queued work without requiring a worker thread.
    bool got_scheduler = false;
    struct receiver {
        using receiver_concept = ex::receiver_t;
        ex::run_loop::scheduler expected;
        bool* result;
        void set_value(const ex::run_loop::scheduler& value) && noexcept { *result = value == expected; }
        void set_error(std::exception_ptr) && noexcept { *result = false; }
        void set_stopped() && noexcept { *result = false; }
    };
    auto operation = ex::connect(std::move(sender), receiver{scheduler, &got_scheduler});
    ex::start(operation);
    loop.finish();
    loop.run();
    EXPECT_TRUE(got_scheduler);
}

TEST(SchedulerEnvQueriesTest, CountingScopeFiltersBaseQueriesAndOverridesStopToken) {
    ex::counting_scope scope;
    std::inplace_stop_source downstream;
    auto inspect = [token = downstream.get_token()](const auto& env) noexcept {
        static_assert(!has_local<std::remove_cvref_t<decltype(env)>>);
        auto actual = ex::get_stop_token(env);
        static_assert(std::stoppable_token<decltype(actual)>);
        if constexpr (std::same_as<decltype(actual), decltype(token)>) {
            return env.query(forwarding_key{}) == 10 && actual != token;
        } else {
            static_assert(std::same_as<decltype(actual),
                ex::__forge_counting_scope::__fused_stop_token<std::inplace_stop_token>>);
            return env.query(forwarding_key{}) == 10 && actual.stop_possible() && !actual.stop_requested();
        }
    };
    auto base = ex::make_env(probe_env{10, 20}, ex::prop(ex::get_stop_token, downstream.get_token()));
    auto result = forge::sync_wait(ex::write_env(
        scope.get_token().wrap(ex::read_env(inspect)), base));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    (void)forge::sync_wait(scope.join());
}

TEST(SchedulerEnvQueriesTest, SpawnFutureFiltersOverlayQueriesAndOverridesStopToken) {
    ex::simple_counting_scope scope;
    std::inplace_stop_source downstream;
    auto inspect = [token = downstream.get_token()](const auto& env) noexcept {
        static_assert(!has_local<std::remove_cvref_t<decltype(env)>>);
        auto actual = ex::get_stop_token(env);
        static_assert(std::stoppable_token<decltype(actual)>);
        if constexpr (std::same_as<decltype(actual), decltype(token)>) {
            return env.query(forwarding_key{}) == 10 && actual != token;
        } else {
            static_assert(std::same_as<decltype(actual),
                ex::__forge_counting_scope::__fused_stop_token<std::inplace_stop_token>>);
            return env.query(forwarding_key{}) == 10 && actual.stop_possible() && !actual.stop_requested();
        }
    };
    auto base = ex::make_env(probe_env{10, 20}, ex::prop(ex::get_stop_token, downstream.get_token()));
    auto future = ex::spawn_future(ex::read_env(inspect), scope.get_token(), base);
    auto result = forge::sync_wait(std::move(future));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    (void)forge::sync_wait(scope.join());
}

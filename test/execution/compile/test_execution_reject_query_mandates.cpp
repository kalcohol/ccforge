#include <execution>
#include <memory>
namespace ex = std::execution;
#ifndef FORGE_QUERY_CASE
#define FORGE_QUERY_CASE 0
#endif
#ifndef FORGE_QUERY_POSITIVE
#define FORGE_QUERY_POSITIVE 0
#endif

template<class Q, class R, bool Nothrow>
struct environment {
    R query(Q) const noexcept(Nothrow) { return R{}; }
};
template<class R, bool Nothrow>
struct forwarding_probe {
    constexpr bool operator()(const ex::env<>&) const noexcept { return false; }
    constexpr R query(std::forwarding_query_t) const noexcept(Nothrow) { return R{}; }
};

template<class R, bool Nothrow>
struct env_holder {
    R get_env() const noexcept(Nothrow) { return R{}; }
};
template<class Scheduler>
struct schedule_sender : decltype(ex::just()) {
    auto get_env() const noexcept -> Scheduler { return {}; }
};
template<class R, bool Nothrow>
struct progress_scheduler : environment<ex::get_forward_progress_guarantee_t, R, Nothrow> {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept -> schedule_sender<progress_scheduler> { return {}; }
    bool operator==(const progress_scheduler&) const noexcept { return true; }
};
struct no_default { no_default() = delete; };
struct throwing_default { throwing_default() noexcept(false) {} };
struct nothrow_default { nothrow_default() noexcept {} };
using tested_default = std::conditional_t<FORGE_QUERY_POSITIVE, nothrow_default, throwing_default>;
struct nonvoid_operation {
    int start() & noexcept { return 0; }
};
struct nonsender_schedule {
    int schedule() const noexcept { return 0; }
};
struct nonvoid_receiver {
    int set_value() && noexcept { return 0; }
    int set_error(int) && noexcept { return 0; }
    int set_stopped() && noexcept { return 0; }
};
struct candidate_domain {
    template<class Tag, class S, class E>
    static decltype(auto) transform_sender(Tag, S&& sndr, const E&) noexcept {
#if FORGE_QUERY_POSITIVE
        return static_cast<S&&>(sndr);
#else
        return 0;
#endif
    }
};
using scheduler = ex::inline_scheduler;
using progress = ex::forward_progress_guarantee;
using completion_query = ex::get_completion_scheduler_t<ex::set_value_t>;
struct chain_scheduler : environment<completion_query, scheduler, false> {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    progress query(ex::get_forward_progress_guarantee_t) const noexcept { return progress::weakly_parallel; }
    using environment::query;
    bool operator==(const chain_scheduler&) const noexcept { return true; }
};

struct lvalue_scheduler : progress_scheduler<progress, true> {
#if FORGE_QUERY_POSITIVE
    auto schedule() const noexcept -> schedule_sender<lvalue_scheduler> { return {}; }
#else
    auto schedule() & noexcept -> schedule_sender<lvalue_scheduler> { return {}; }
#endif
};
struct lvalue_scheduler_environment {
    lvalue_scheduler* value;
    lvalue_scheduler& query(ex::get_scheduler_t) const noexcept { return *value; }
};

// forwarding_query(Q{}) is true when it is a constant expression.
struct default_classified : std::forwarding_query_t {
#if FORGE_QUERY_POSITIVE
    constexpr default_classified() noexcept {}
#else
    default_classified() noexcept {}
#endif
};
struct default_classified_environment {
    int query(default_classified) const noexcept { return 1; }
};
template<class Env, class Query>
concept answers_query = requires(const Env& env, Query query) { env.query(query); };

int main() {
#if FORGE_QUERY_CASE == 0
    static_assert(std::same_as<decltype(std::get_stop_token(ex::env<>{})), std::never_stop_token>);
    static_assert(ex::scheduler<scheduler>);
    ex::get_env(env_holder<ex::env<>, true>{});
    std::get_stop_token(environment<std::get_stop_token_t, std::never_stop_token, true>{});
    std::get_allocator(environment<std::get_allocator_t, std::allocator<int>, true>{});
    ex::get_scheduler(environment<ex::get_scheduler_t, scheduler, true>{});
    ex::get_start_scheduler(environment<ex::get_start_scheduler_t, scheduler, true>{});
    ex::get_delegation_scheduler(environment<ex::get_delegation_scheduler_t, scheduler, true>{});
    ex::get_forward_progress_guarantee(progress_scheduler<progress, true>{});
    ex::get_completion_scheduler<ex::set_value_t>(environment<completion_query, scheduler, true>{});
    ex::get_await_completion_adaptor(environment<ex::get_await_completion_adaptor_t, int, true>{});
    static_assert(!std::forwarding_query(forwarding_probe<bool, true>{}));
    ex::get_domain(environment<ex::get_domain_t, ex::default_domain, true>{});
    ex::get_completion_domain<>(environment<ex::get_completion_domain_t<>, ex::default_domain, true>{});
#elif FORGE_QUERY_CASE == 1
    ex::get_env(env_holder<ex::env<>, false>{});
#elif FORGE_QUERY_CASE == 2
    std::get_stop_token(environment<std::get_stop_token_t, std::never_stop_token, false>{});
#elif FORGE_QUERY_CASE == 3
    std::get_allocator(environment<std::get_allocator_t, std::allocator<int>, false>{});
#elif FORGE_QUERY_CASE == 4
    ex::get_scheduler(environment<ex::get_scheduler_t, scheduler, false>{});
#elif FORGE_QUERY_CASE == 5
    ex::get_start_scheduler(environment<ex::get_start_scheduler_t, scheduler, false>{});
#elif FORGE_QUERY_CASE == 6
    ex::get_delegation_scheduler(environment<ex::get_delegation_scheduler_t, scheduler, false>{});
#elif FORGE_QUERY_CASE == 7
    ex::get_forward_progress_guarantee(progress_scheduler<progress, false>{});
#elif FORGE_QUERY_CASE == 8
    ex::get_completion_scheduler<ex::set_value_t>(environment<completion_query, scheduler, false>{});
#elif FORGE_QUERY_CASE == 9
    ex::get_await_completion_adaptor(environment<ex::get_await_completion_adaptor_t, int, false>{});
#elif FORGE_QUERY_CASE == 10
    static_assert(!std::forwarding_query(forwarding_probe<bool, FORGE_QUERY_POSITIVE>{}));
#elif FORGE_QUERY_CASE == 11
    using result = std::conditional_t<FORGE_QUERY_POSITIVE, bool, int>;
    static_assert(!std::forwarding_query(forwarding_probe<result, true>{}));
#elif FORGE_QUERY_CASE == 12
    std::get_stop_token(environment<std::get_stop_token_t, int, true>{});
#elif FORGE_QUERY_CASE == 13
    std::get_allocator(environment<std::get_allocator_t, int, true>{});
#elif FORGE_QUERY_CASE == 14
    ex::get_scheduler(environment<ex::get_scheduler_t, int, true>{});
#elif FORGE_QUERY_CASE == 15
    ex::get_start_scheduler(environment<ex::get_start_scheduler_t, int, true>{});
#elif FORGE_QUERY_CASE == 16
    ex::get_delegation_scheduler(environment<ex::get_delegation_scheduler_t, int, true>{});
#elif FORGE_QUERY_CASE == 17
    ex::get_forward_progress_guarantee(progress_scheduler<int, true>{});
#elif FORGE_QUERY_CASE == 18
    ex::get_completion_scheduler<ex::set_value_t>(environment<completion_query, int, true>{});
#elif FORGE_QUERY_CASE == 19
    ex::get_env(env_holder<void, true>{});
#elif FORGE_QUERY_CASE == 20
    ex::get_domain(environment<ex::get_domain_t, no_default, true>{});
#elif FORGE_QUERY_CASE == 21
    ex::get_domain(environment<ex::get_domain_t, tested_default, true>{});
#elif FORGE_QUERY_CASE == 22
    ex::get_completion_domain<>(environment<ex::get_completion_domain_t<>, no_default, true>{});
#elif FORGE_QUERY_CASE == 23
    ex::get_completion_domain<>(environment<ex::get_completion_domain_t<>, tested_default, true>{});
#elif FORGE_QUERY_CASE == 24
    nonvoid_operation op;
    ex::start(op);
#elif FORGE_QUERY_CASE == 25
    ex::schedule(nonsender_schedule{});
#elif FORGE_QUERY_CASE == 26
    ex::set_value(nonvoid_receiver{});
#elif FORGE_QUERY_CASE == 27
    ex::set_error(nonvoid_receiver{}, 1);
#elif FORGE_QUERY_CASE == 28
    ex::set_stopped(nonvoid_receiver{});
#elif FORGE_QUERY_CASE == 29
    ex::completion_signatures<void()> invalid;
    (void)invalid;
#elif FORGE_QUERY_CASE == 30
#if FORGE_QUERY_POSITIVE
    using source = decltype(ex::just());
    using candidate_result = decltype(candidate_domain::transform_sender(
        ex::start, std::declval<source>(), ex::env<>{}));
    using default_result = decltype(ex::default_domain::transform_sender(
        ex::start, std::declval<source>(), ex::env<>{}));
    static_assert(std::same_as<candidate_result, source&&>);
    static_assert(std::same_as<default_result, source>);
    static_assert(std::same_as<std::decay_t<candidate_result>, std::decay_t<default_result>>);
#endif
    ex::indeterminate_domain<candidate_domain>::transform_sender(ex::start, ex::just(), ex::env<>{});
#elif FORGE_QUERY_CASE == 31
    ex::get_completion_scheduler<ex::set_value_t>(environment<completion_query, chain_scheduler, true>{});
#elif FORGE_QUERY_CASE == 32
    ex::transform_sender(ex::just(), environment<ex::get_domain_t, tested_default, true>{});
#elif FORGE_QUERY_CASE == 33
    struct source {
        using sender_concept = ex::sender_tag;
        auto get_env() const noexcept {
            return environment<ex::get_completion_domain_t<>, tested_default, true>{};
        }
    };
    ex::transform_sender(source{}, ex::env<>{});
#elif FORGE_QUERY_CASE == 34
    lvalue_scheduler sch;
    ex::get_completion_scheduler<ex::set_error_t>(sch, ex::env<>{});
#elif FORGE_QUERY_CASE == 35
    lvalue_scheduler sch;
    ex::get_scheduler(lvalue_scheduler_environment{&sch});
#elif FORGE_QUERY_CASE == 36
    // [exec.let]/8.2 applies the forwarding-query concept to the query type.
    // A non-constant forwarding_query(Q{}) is ill-formed ([temp.constr.atomic]).
    // The negative branch only evaluates the concept: it compiles whether a
    // broken implementation drops the query or accepts it.
    using connected = ex::__forge_let::__connected_env_t<
        ex::env<>, default_classified_environment>;
#if FORGE_QUERY_POSITIVE
    static_assert(answers_query<connected, default_classified>);
#else
    [[maybe_unused]] constexpr bool evaluated = answers_query<connected, default_classified>;
#endif
#endif
}

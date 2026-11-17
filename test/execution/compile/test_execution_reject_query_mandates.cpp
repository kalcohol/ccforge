#include <execution>
#include <memory>
namespace ex = std::execution;
#ifndef FORGE_QUERY_CASE
#define FORGE_QUERY_CASE 0
#endif
#ifndef FORGE_QUERY_POSITIVE
#define FORGE_QUERY_POSITIVE 0
#endif
#ifndef FORGE_QUERY_LEGACY
#define FORGE_QUERY_LEGACY 0
#endif

template<class Q, class R, bool Nothrow>
struct environment {
#if FORGE_QUERY_LEGACY
    friend R tag_invoke(Q, const environment&) noexcept(Nothrow) { return R{}; }
#else
    R query(Q) const noexcept(Nothrow) { return R{}; }
#endif
};
template<class R, bool Nothrow>
struct env_holder {
#if FORGE_QUERY_LEGACY
    friend R tag_invoke(ex::get_env_t, const env_holder&) noexcept(Nothrow) { return R{}; }
#else
    R get_env() const noexcept(Nothrow) { return R{}; }
#endif
};
template<class R, bool Nothrow>
struct progress_scheduler : environment<ex::get_forward_progress_guarantee_t, R, Nothrow> {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    bool operator==(const progress_scheduler&) const noexcept { return true; }
};
struct no_default { no_default() = delete; };
struct throwing_default { throwing_default() noexcept(false) {} };
struct nothrow_default { nothrow_default() noexcept {} };
using tested_default = std::conditional_t<FORGE_QUERY_POSITIVE, nothrow_default, throwing_default>;
struct nonvoid_operation {
#if FORGE_QUERY_LEGACY
    friend int tag_invoke(ex::start_t, nonvoid_operation&) noexcept { return 0; }
#else
    int start() & noexcept { return 0; }
#endif
};
struct nonsender_schedule {
#if FORGE_QUERY_LEGACY
    friend int tag_invoke(ex::schedule_t, nonsender_schedule) noexcept { return 0; }
#else
    int schedule() const noexcept { return 0; }
#endif
};
struct nonvoid_receiver {
#if FORGE_QUERY_LEGACY
    friend int tag_invoke(ex::set_value_t, nonvoid_receiver&&) noexcept { return 0; }
    friend int tag_invoke(ex::set_error_t, nonvoid_receiver&&, int) noexcept { return 0; }
    friend int tag_invoke(ex::set_stopped_t, nonvoid_receiver&&) noexcept { return 0; }
#else
    int set_value() && noexcept { return 0; }
    int set_error(int) && noexcept { return 0; }
    int set_stopped() && noexcept { return 0; }
#endif
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
#if !FORGE_QUERY_LEGACY
    using environment::query;
#endif
    bool operator==(const chain_scheduler&) const noexcept { return true; }
};

struct lvalue_scheduler : progress_scheduler<progress, true> {
#if FORGE_QUERY_POSITIVE
    auto schedule() const noexcept { return ex::just(); }
#else
    auto schedule() & noexcept { return ex::just(); }
#endif
};
struct lvalue_scheduler_environment {
    lvalue_scheduler* value;
#if FORGE_QUERY_LEGACY
    friend lvalue_scheduler& tag_invoke(ex::get_scheduler_t, const lvalue_scheduler_environment& e) noexcept {
        return *e.value;
    }
#else
    lvalue_scheduler& query(ex::get_scheduler_t) const noexcept { return *value; }
#endif
};

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
    std::forwarding_query(environment<std::forwarding_query_t, bool, true>{});
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
    std::forwarding_query(environment<std::forwarding_query_t, bool, false>{});
#elif FORGE_QUERY_CASE == 11
    std::forwarding_query(environment<std::forwarding_query_t, int, true>{});
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
#endif
}

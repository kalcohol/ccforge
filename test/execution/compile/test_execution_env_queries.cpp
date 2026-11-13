// Include fragments directly so native stand-aside cannot hide regressions.
#include <forge/execution_extensions.hpp>
#include "cpp26/execution/sync_wait.hpp"
#include "cpp26/execution/inline_scheduler.hpp"
#include "cpp26/execution/let.hpp"
#include "cpp26/execution/when_all.hpp"
#include "cpp26/execution/write_env.hpp"
#include "cpp26/execution/spawn_future.hpp"
#include "cpp26/execution/counting_scope.hpp"
#include "cpp26/execution/continues_on.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <utility>

namespace ex = std::execution;
namespace {
struct missing {};
struct key {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
struct local_key {};
struct variadic_key {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
struct answer {
    int value;
    constexpr const int& query(key) const noexcept { return value; }
    constexpr int query(variadic_key, int& n, int&& m) const noexcept {
        return value + n + m;
    }
    constexpr int query(local_key) const noexcept { return value; }
};
struct throwing_answer {
    constexpr long query(key) const { return 99; }
};
struct legacy {
    friend constexpr int tag_invoke(key, const legacy&) noexcept { return 7; }
};
struct nondefault_key { nondefault_key() = delete; };
struct nondefault_answer {
    int query(nondefault_key) const noexcept;
};
template<class E, class Q, class... Args>
concept answers = requires(const E& e, Q q, Args&&... args) {
    e.query(q, static_cast<Args&&>(args)...);
};
using composed = ex::env<forge::empty_env, answer, throwing_answer>;
static_assert(answers<composed, key>);
static_assert(!answers<composed, missing>);
static_assert(!answers<ex::env<>, key>);
static_assert(!answers<ex::env<legacy>, key>);
static_assert(!answers<ex::env<nondefault_answer>, nondefault_key>);
static_assert(std::same_as<decltype(std::declval<const composed&>().query(key{})), const int&>);
static_assert(noexcept(std::declval<const composed&>().query(key{})));
static_assert(!noexcept(std::declval<const ex::env<throwing_answer, answer>&>().query(key{})));
constexpr composed values{forge::empty_env{}, answer{42}, throwing_answer{}};
static_assert(values.query(key{}) == 42);
static_assert(&values.query(key{}) == &values.query(key{}));
constexpr bool variadic_selection() {
    int n = 3;
    return values.query(variadic_key{}, n, 4) == 49;
}
static_assert(variadic_selection());
using mixed = ex::env<legacy, answer>;
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(std::same_as<decltype(std::declval<const mixed&>().query(key{})), const int&>);
static_assert(mixed{legacy{}, answer{42}}.query(key{}) == 42);
#else
static_assert(std::same_as<decltype(std::declval<const mixed&>().query(key{})), int>);
static_assert(mixed{legacy{}, answer{42}}.query(key{}) == 7);
static_assert(ex::__forge_detail::tag_invocable<key, const ex::env<legacy>&>);
static_assert(ex::__forge_detail::tag_invoke_fn(key{}, mixed{legacy{}, answer{42}}) == 7);
#endif

// Participation uses Q(), while selection and noexcept use the named lvalue q.
struct category_key {};
struct rvalue_only {
    constexpr short query(category_key&&) const noexcept { return 1; }
};
struct named_answer {
    constexpr int query(const category_key&) const { return 2; }
};
struct category_answer {
    constexpr int query(category_key&) const { return 3; }
    constexpr long query(const category_key&) const noexcept { return 4; }
    constexpr short query(category_key&&) const noexcept { return 5; }
};
using category_env = ex::env<rvalue_only, named_answer>;
static_assert(std::same_as<decltype(std::declval<const category_env&>().query(category_key{})), int>);
static_assert(category_env{rvalue_only{}, named_answer{}}.query(category_key{}) == 2);
static_assert(!noexcept(std::declval<const category_env&>().query(category_key{})));
using overload_env = ex::env<category_answer>;
static_assert(std::same_as<decltype(std::declval<const overload_env&>().query(category_key{})), int>);
static_assert(overload_env{category_answer{}}.query(category_key{}) == 3);
static_assert(!noexcept(std::declval<const overload_env&>().query(category_key{})));
struct nothrow_named_answer {
    constexpr int query(category_key&) const noexcept { return 9; }
    constexpr long query(const category_key&) const { return 10; }
    constexpr short query(category_key&&) const { return 11; }
};
using nothrow_named_env = ex::env<nothrow_named_answer>;
static_assert(nothrow_named_env{nothrow_named_answer{}}.query(category_key{}) == 9);
static_assert(noexcept(std::declval<const nothrow_named_env&>().query(category_key{})));
struct lvalue_only {
    constexpr int query(category_key&) const noexcept { return 6; }
};
// A named-lvalue answer alone does not satisfy the draft participation fold.
static_assert(!answers<ex::env<lvalue_only>, category_key>);
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
using lvalue_env = ex::env<lvalue_only, rvalue_only>;
static_assert(lvalue_env{lvalue_only{}, rvalue_only{}}.query(category_key{}) == 6);
static_assert(noexcept(std::declval<const lvalue_env&>().query(category_key{})));
#endif

struct legacy_queries {
    friend ex::inline_scheduler tag_invoke(ex::get_scheduler_t, const legacy_queries&) noexcept;
    friend std::inplace_stop_token tag_invoke(ex::get_stop_token_t, const legacy_queries&) noexcept;
};
struct member_queries {
    ex::run_loop::scheduler query(ex::get_scheduler_t) const noexcept;
    std::never_stop_token query(ex::get_stop_token_t) const noexcept;
};
struct dual_queries : member_queries {
    friend ex::inline_scheduler tag_invoke(ex::get_scheduler_t, const dual_queries&) noexcept;
};
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const ex::env<dual_queries>&>())),
                           ex::run_loop::scheduler>);
struct throwing_legacy_queries {
    friend ex::inline_scheduler tag_invoke(ex::get_scheduler_t, const throwing_legacy_queries&);
};
using throwing_mixed_queries = ex::env<throwing_legacy_queries, member_queries>;
using mixed_queries = ex::env<legacy_queries, member_queries>;
using member_first_queries = ex::env<member_queries, legacy_queries>;
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const member_first_queries&>())),
                           ex::run_loop::scheduler>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const member_first_queries&>())),
                           std::never_stop_token>);
static_assert(noexcept(ex::get_scheduler(std::declval<const mixed_queries&>())));
static_assert(noexcept(ex::get_stop_token(std::declval<const mixed_queries&>())));
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(noexcept(ex::get_scheduler(std::declval<const throwing_mixed_queries&>())));
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const mixed_queries&>())),
                           ex::run_loop::scheduler>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const mixed_queries&>())),
                           std::never_stop_token>);
#else
// Ordinary CPO calls must retain the earlier tag-only scheduler and stop token.
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const ex::env<legacy_queries>&>())),
                           ex::inline_scheduler>);
static_assert(!noexcept(ex::get_scheduler(std::declval<const throwing_mixed_queries&>())));
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const mixed_queries&>())),
                           ex::inline_scheduler>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const mixed_queries&>())),
                           std::inplace_stop_token>);
struct throwing_legacy {
    friend constexpr int tag_invoke(key, const throwing_legacy&) { return 8; }
};
static_assert(!noexcept(std::declval<const ex::env<throwing_legacy, answer>&>().query(key{})));
#endif
// Borrowed children keep their referent qualification even in a const env.
template<bool Nothrow>
struct borrowed_member {
    constexpr int query(key) & noexcept(Nothrow) { return 17; }
    constexpr long query(key) const& noexcept(!Nothrow) { return 18; }
};
using borrowed_member_env = ex::env<borrowed_member<true>&, answer>;
using throwing_borrowed_member_env = ex::env<borrowed_member<false>&, answer>;
static_assert(std::same_as<decltype(std::declval<const borrowed_member_env&>().query(key{})), int>);
static_assert(noexcept(std::declval<const borrowed_member_env&>().query(key{})));
static_assert(!noexcept(std::declval<const throwing_borrowed_member_env&>().query(key{})));
using const_borrowed_env = ex::env<const borrowed_member<true>&, answer>;
static_assert(std::same_as<decltype(std::declval<const const_borrowed_env&>().query(key{})), long>);
static_assert(!noexcept(std::declval<const const_borrowed_env&>().query(key{})));
constexpr bool borrowed_member_selection() {
    borrowed_member<true> child;
    const auto e = ex::env{std::ref(child), answer{42}};
    return e.query(key{}) == 17;
}
static_assert(borrowed_member_selection());

struct mutable_member_queries {
    ex::inline_scheduler query(ex::get_scheduler_t) & noexcept;
    std::inplace_stop_token query(ex::get_stop_token_t) &;
};
template<bool Nothrow>
struct mutable_legacy_queries {
    friend ex::inline_scheduler tag_invoke(
        ex::get_scheduler_t, mutable_legacy_queries&) noexcept(Nothrow) { return {}; }
    friend std::inplace_stop_token tag_invoke(
        ex::get_stop_token_t, mutable_legacy_queries&) noexcept(Nothrow) { return {}; }
};
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
// Only strict env selection bypasses the shared compatibility const-member probe.
using mutable_borrowed_env = ex::env<mutable_member_queries&, member_queries>;
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const mutable_borrowed_env&>())),
                           ex::inline_scheduler>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const mutable_borrowed_env&>())),
                           std::inplace_stop_token>);
static_assert(noexcept(ex::get_scheduler(std::declval<const mutable_borrowed_env&>())));
static_assert(!noexcept(ex::get_stop_token(std::declval<const mutable_borrowed_env&>())));
#else
using mutable_borrowed_env = ex::env<mutable_legacy_queries<true>&, member_queries>;
using throwing_borrowed_env = ex::env<mutable_legacy_queries<false>&, member_queries>;
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const mutable_borrowed_env&>())),
                           ex::inline_scheduler>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const mutable_borrowed_env&>())),
                           std::inplace_stop_token>);
static_assert(noexcept(ex::get_scheduler(std::declval<const mutable_borrowed_env&>())));
static_assert(noexcept(ex::get_stop_token(std::declval<const mutable_borrowed_env&>())));
static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const throwing_borrowed_env&>())),
                           ex::inline_scheduler>);
static_assert(!noexcept(ex::get_scheduler(std::declval<const throwing_borrowed_env&>())));
static_assert(!noexcept(ex::get_stop_token(std::declval<const throwing_borrowed_env&>())));
#endif
using stop_prop = ex::prop<ex::get_stop_token_t, std::inplace_stop_token>;
using alloc_prop = ex::prop<ex::get_allocator_t, std::allocator<int>>;
using sched_prop = ex::prop<ex::get_start_scheduler_t, ex::inline_scheduler>;
static_assert(std::same_as<decltype(std::declval<const stop_prop&>().query(ex::get_stop_token)),
                           const std::inplace_stop_token&>);
static_assert(noexcept(std::declval<const stop_prop&>().query(ex::get_stop_token)));
static_assert(answers<ex::env<stop_prop, alloc_prop, sched_prop>, ex::get_stop_token_t>);
static_assert(answers<ex::env<stop_prop, alloc_prop, sched_prop>, ex::get_allocator_t>);
static_assert(answers<ex::env<stop_prop, alloc_prop, sched_prop>, ex::get_start_scheduler_t>);
static_assert(answers<alloc_prop, ex::get_allocator_t, int>);
static_assert(!answers<alloc_prop, missing>);
constexpr bool borrowed_prop() {
    int n = 1;
    auto p = ex::prop{key{}, std::ref(n)};
    auto e = ex::env{std::ref(p)};
    return &p.query(key{}) == &n && &e.query(key{}) == &n;
}
static_assert(borrowed_prop());

template<class E, class Q, class T>
constexpr bool nothrow_answer = std::same_as<decltype(std::declval<const E&>().query(Q{})), T>
    && noexcept(std::declval<const E&>().query(Q{}));
using sync_env = ex::__forge_sync_wait::sync_wait_env;
static_assert(nothrow_answer<sync_env, ex::get_scheduler_t, ex::run_loop::scheduler>);
static_assert(nothrow_answer<sync_env, ex::get_start_scheduler_t, ex::run_loop::scheduler>);
static_assert(nothrow_answer<sync_env, ex::get_delegation_scheduler_t, ex::run_loop::scheduler>);
static_assert(!answers<sync_env, missing>);
static_assert(!answers<sync_env, ex::get_stop_token_t>);
static_assert(!answers<sync_env, ex::get_scheduler_t, int>);
using child_env = ex::__forge_when_all::__child_env<answer>;
static_assert(nothrow_answer<child_env, ex::get_stop_token_t, std::inplace_stop_token>);
static_assert(nothrow_answer<child_env, key, const int&>);
static_assert(!answers<child_env, local_key>);
static_assert(!answers<child_env, ex::get_stop_token_t, int>);
static_assert(!noexcept(std::declval<const ex::__forge_when_all::__child_env<throwing_answer>&>().query(key{})));
using spawn_env = ex::__forge_spawn_future::__spawn_env<answer>;
static_assert(nothrow_answer<spawn_env, ex::get_stop_token_t, std::inplace_stop_token>);
static_assert(nothrow_answer<spawn_env, key, const int&>);
static_assert(!answers<spawn_env, local_key>);
static_assert(!answers<spawn_env, missing>);
static_assert(!answers<spawn_env, ex::get_stop_token_t, int>);
static_assert(!noexcept(std::declval<const ex::__forge_spawn_future::__spawn_env<throwing_answer>&>().query(key{})));
using scope_env = ex::__forge_counting_scope::__stop_env<answer>;
static_assert(nothrow_answer<scope_env, ex::get_stop_token_t, std::inplace_stop_token>);
static_assert(nothrow_answer<scope_env, key, const int&>);
static_assert(!answers<scope_env, local_key>);
static_assert(!answers<scope_env, missing>);
static_assert(!answers<scope_env, ex::get_stop_token_t, int>);
static_assert(!noexcept(std::declval<const ex::__forge_counting_scope::__stop_env<throwing_answer>&>().query(key{})));
using fused_token = ex::__forge_counting_scope::__fused_stop_token<std::inplace_stop_token>;
using fused_env = ex::__forge_counting_scope::__stop_env<answer, fused_token>;
static_assert(nothrow_answer<fused_env, ex::get_stop_token_t, fused_token>);
static_assert(nothrow_answer<fused_env, key, const int&>);
static_assert(!answers<fused_env, local_key>);
static_assert(std::stoppable_token<fused_token>);

template<class Overlay, class ExpectedToken>
constexpr bool stop_when_write_env_signatures = std::same_as<
    ex::completion_signatures_of_t<decltype(ex::write_env(
        std::declval<ex::__forge_counting_scope::__stop_sender<
            decltype(ex::read_env(ex::get_stop_token))>>(), std::declval<Overlay>())),
        forge::empty_env>,
    ex::completion_signatures<ex::set_value_t(ExpectedToken), ex::set_stopped_t()>>;
using allocator_prop = ex::prop<ex::get_allocator_t, std::allocator<std::byte>>;
static_assert(stop_when_write_env_signatures<forge::empty_env, std::inplace_stop_token>);
static_assert(stop_when_write_env_signatures<allocator_prop, std::inplace_stop_token>);
static_assert(stop_when_write_env_signatures<stop_prop, fused_token>);
static_assert(stop_when_write_env_signatures<ex::env<allocator_prop, stop_prop>, fused_token>);
using forwarding_env = ex::__forge_let::__forwarding_env<answer>;
static_assert(nothrow_answer<forwarding_env, key, const int&>);
static_assert(!answers<forwarding_env, local_key>);
static_assert(!noexcept(std::declval<const ex::__forge_let::__forwarding_env<throwing_answer>&>().query(key{})));
using scheduler_env = ex::__forge_let::__scheduler_env<ex::inline_scheduler>;
static_assert(nothrow_answer<scheduler_env, ex::get_start_scheduler_t, ex::inline_scheduler>);
static_assert(!answers<scheduler_env, ex::get_scheduler_t>);
static_assert(!answers<scheduler_env, ex::get_domain_t>);
using inner_env = ex::__forge_let::__inner_env<answer, throwing_answer>;
static_assert(nothrow_answer<inner_env, key, const int&>);
static_assert(answers<inner_env, local_key>);
using outer_only_env = ex::__forge_let::__inner_env<forge::empty_env, answer>;
static_assert(nothrow_answer<outer_only_env, key, const int&>);
static_assert(!answers<outer_only_env, local_key>);
static_assert(!answers<inner_env, missing>);
static_assert(!answers<forwarding_env, missing>);
static_assert(!answers<child_env, missing>);
static_assert(!answers<child_env, variadic_key, int&, int>);
static_assert(answers<forwarding_env, variadic_key, int&, int>);
static_assert(answers<inner_env, variadic_key, int&, int>);
static_assert(!answers<spawn_env, variadic_key, int&, int>);
struct domain {};
struct domain_scheduler : ex::inline_scheduler {
    domain query(ex::get_domain_t) const noexcept { return {}; }
    template<class CPO>
    domain query(ex::get_completion_domain_t<CPO>, const answer&) const noexcept {
        return {};
    }
};
using domain_env = ex::__forge_let::__scheduler_env<domain_scheduler>;
static_assert(nothrow_answer<domain_env, ex::get_domain_t, domain>);

// write_env joins the overlay to FWD-ENV(receiver-env), with unary lookup.
using joined_env = ex::__forge_write_env::__joined_env<answer, throwing_answer>;
using base_only_env = ex::__forge_write_env::__joined_env<forge::empty_env, answer>;
using throwing_join = ex::__forge_write_env::__joined_env<throwing_answer, answer>;
static_assert(nothrow_answer<joined_env, key, const int&>);
static_assert(nothrow_answer<base_only_env, key, const int&>);
static_assert(!noexcept(std::declval<const throwing_join&>().query(key{})));
static_assert(answers<joined_env, local_key>);
static_assert(!answers<base_only_env, local_key>);
static_assert(!ex::__forge_env_detail::__queryable<local_key, const base_only_env&>);
static_assert(!ex::__forge_detail::tag_invocable<local_key, const base_only_env&>);
static_assert(!answers<joined_env, missing>);
static_assert(!answers<joined_env, variadic_key, int&, int>);
constexpr bool joined_selection() {
    answer overlay{12};
    answer base{34};
    ex::__forge_write_env::__joined_env<answer, answer> both{&overlay, &base};
    forge::empty_env empty;
    base_only_env filtered{&empty, &base};
    return &both.query(key{}) == &overlay.value && both.query(local_key{}) == 12 &&
           &filtered.query(key{}) == &base.value;
}
static_assert(joined_selection());
static_assert(std::same_as<decltype(std::declval<joined_env&&>().query(key{})), const int&>);
using legacy_join = ex::__forge_write_env::__joined_env<legacy, answer>;
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(nothrow_answer<legacy_join, key, const int&>);
#else
static_assert(nothrow_answer<legacy_join, key, int>);
#endif

using loop_env = ex::__forge_run_loop::__env;
using loop_scheduler = ex::run_loop::scheduler;
using value_scheduler_query = ex::get_completion_scheduler_t<ex::set_value_t>;
using stopped_scheduler_query = ex::get_completion_scheduler_t<ex::set_stopped_t>;
using error_scheduler_query = ex::get_completion_scheduler_t<ex::set_error_t>;
using value_domain_query = ex::get_completion_domain_t<ex::set_value_t>;
using error_domain_query = ex::get_completion_domain_t<ex::set_error_t>;
using stopped_domain_query = ex::get_completion_domain_t<ex::set_stopped_t>;
static_assert(nothrow_answer<loop_env, ex::get_scheduler_t, loop_scheduler>);
static_assert(nothrow_answer<loop_env, value_scheduler_query, loop_scheduler>);
static_assert(nothrow_answer<loop_env, stopped_scheduler_query, loop_scheduler>);
static_assert(nothrow_answer<loop_scheduler, value_scheduler_query, loop_scheduler>);
static_assert(nothrow_answer<loop_scheduler, stopped_scheduler_query, loop_scheduler>);
static_assert(nothrow_answer<loop_scheduler, ex::get_forward_progress_guarantee_t,
                             ex::forward_progress_guarantee>);
static_assert(!answers<loop_env, error_scheduler_query>);
static_assert(!answers<loop_scheduler, error_scheduler_query>);
static_assert(!answers<loop_env, value_domain_query>);
static_assert(!answers<loop_scheduler, value_domain_query>);
static_assert(!answers<loop_env, stopped_domain_query>);
static_assert(!answers<loop_scheduler, stopped_domain_query>);
static_assert(!answers<loop_env, error_domain_query>);
static_assert(!answers<loop_scheduler, error_domain_query>);
static_assert(!answers<loop_env, missing>);
static_assert(!answers<loop_scheduler, missing>);
static_assert(!answers<loop_env, value_scheduler_query, answer>);
static_assert(!answers<loop_scheduler, value_scheduler_query, answer>);
template<class E, class Q, class... Args>
concept cpo_answers = requires(E&& e, Args&&... args) {
    Q{}(static_cast<E&&>(e), static_cast<Args&&>(args)...);
};
static_assert(!cpo_answers<loop_env, error_scheduler_query>);
static_assert(!cpo_answers<loop_scheduler, error_scheduler_query>);
static_assert(!cpo_answers<loop_env, value_domain_query>);
static_assert(!cpo_answers<loop_scheduler, value_domain_query>);
static_assert(!cpo_answers<loop_env, stopped_domain_query>);
static_assert(!cpo_answers<loop_scheduler, stopped_domain_query>);
static_assert(!cpo_answers<loop_env, error_domain_query>);
static_assert(!cpo_answers<loop_scheduler, error_domain_query>);
static_assert(!cpo_answers<loop_env, ex::get_completion_domain_t<>>);
static_assert(!cpo_answers<loop_scheduler, ex::get_completion_domain_t<>>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<loop_env&&>())), loop_scheduler>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_stopped_t>(
    std::declval<const loop_scheduler&&>())), loop_scheduler>);
static_assert(noexcept(ex::get_completion_scheduler<ex::set_value_t>(std::declval<const loop_env&>())));
static_assert(noexcept(ex::get_completion_scheduler<ex::set_stopped_t>(std::declval<const loop_scheduler&>())));

// Both sides retain unary domain absence and share the scheduler's fallback
// for nonempty environment packs, including void's value-domain alternative.
template<class Q, class... Envs>
constexpr bool loop_domain_parity =
    cpo_answers<const loop_env&, Q, Envs...> &&
    cpo_answers<const loop_scheduler&, Q, Envs...> &&
    std::same_as<decltype(Q{}(std::declval<const loop_env&>(), std::declval<Envs>()...)),
                 ex::default_domain> &&
    std::same_as<decltype(Q{}(std::declval<const loop_scheduler&>(), std::declval<Envs>()...)),
                 ex::default_domain> &&
    noexcept(Q{}(std::declval<const loop_env&>(), std::declval<Envs>()...)) &&
    noexcept(Q{}(std::declval<const loop_scheduler&>(), std::declval<Envs>()...));
static_assert(loop_domain_parity<value_domain_query, forge::empty_env>);
static_assert(loop_domain_parity<stopped_domain_query, forge::empty_env>);
static_assert(loop_domain_parity<error_domain_query, forge::empty_env>);
static_assert(loop_domain_parity<ex::get_completion_domain_t<>, forge::empty_env>);
static_assert(loop_domain_parity<value_domain_query, ex::prop<ex::get_domain_t, domain>>);
static_assert(loop_domain_parity<value_domain_query, const answer&, forge::empty_env>);
static_assert(loop_domain_parity<stopped_domain_query, const answer&, forge::empty_env>);
static_assert(!cpo_answers<loop_env, ex::get_completion_domain_t<missing>, forge::empty_env>);
static_assert(!cpo_answers<loop_scheduler, ex::get_completion_domain_t<missing>, forge::empty_env>);

// Probe the actual inline attributes, including a different scheduler type.
using inline_attrs = ex::__forge_inline::env;
using sched_env = ex::prop<ex::get_scheduler_t, loop_scheduler>;
using explicit_domain_env = ex::prop<ex::get_domain_t, domain>;
template<class E>
constexpr bool inline_queries =
    std::same_as<decltype(std::declval<const E&>().query(value_scheduler_query{},
                     std::declval<sched_env&>())), const loop_scheduler&> &&
    std::same_as<decltype(std::declval<E&&>().query(value_scheduler_query{},
                     std::declval<const sched_env&&>())), const loop_scheduler&> &&
    noexcept(std::declval<const E&>().query(value_scheduler_query{}, std::declval<sched_env&>())) &&
    std::same_as<decltype(std::declval<const E&>().query(value_domain_query{},
                     std::declval<explicit_domain_env&>())), domain> &&
    noexcept(std::declval<const E&>().query(value_domain_query{}, std::declval<explicit_domain_env&>()));
static_assert(inline_queries<inline_attrs>);
static_assert(inline_queries<ex::inline_scheduler>);
struct throwing_scheduler_env {
    loop_scheduler query(ex::get_scheduler_t) const noexcept(false);
};
static_assert(!noexcept(std::declval<const inline_attrs&>().query(
    value_scheduler_query{}, std::declval<throwing_scheduler_env&>())));
static_assert(!noexcept(std::declval<const ex::inline_scheduler&>().query(
    value_scheduler_query{}, std::declval<throwing_scheduler_env&&>())));
static_assert(!noexcept(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const inline_attrs&>(), std::declval<throwing_scheduler_env&>())));
static_assert(!noexcept(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const ex::inline_scheduler&>(), std::declval<throwing_scheduler_env&&>())));
static_assert(!answers<inline_attrs, ex::get_scheduler_t>);
static_assert(!answers<ex::inline_scheduler, ex::get_scheduler_t>);
static_assert(!answers<inline_attrs, value_scheduler_query>);
static_assert(!answers<ex::inline_scheduler, value_scheduler_query>);
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(!cpo_answers<inline_attrs, value_scheduler_query>);
static_assert(!cpo_answers<ex::inline_scheduler, value_scheduler_query>);
static_assert(!cpo_answers<inline_attrs, value_scheduler_query, forge::empty_env>);
static_assert(!cpo_answers<ex::inline_scheduler, value_scheduler_query, forge::empty_env>);
#else
static_assert(cpo_answers<inline_attrs, value_scheduler_query>);
static_assert(cpo_answers<ex::inline_scheduler, value_scheduler_query>);
static_assert(std::same_as<decltype(value_scheduler_query{}(
    std::declval<const inline_attrs&>(), forge::empty_env{})), ex::inline_scheduler>);
static_assert(std::same_as<decltype(value_scheduler_query{}(
    ex::inline_scheduler{}, forge::empty_env{})), ex::inline_scheduler>);
#endif
static_assert(nothrow_answer<inline_attrs, ex::get_forward_progress_guarantee_t,
                             ex::forward_progress_guarantee>);
static_assert(nothrow_answer<ex::inline_scheduler, ex::get_forward_progress_guarantee_t,
                             ex::forward_progress_guarantee>);
static_assert(ex::get_forward_progress_guarantee(ex::inline_scheduler{}) ==
              ex::forward_progress_guarantee::weakly_parallel);
static_assert(!answers<inline_attrs, error_scheduler_query, sched_env>);
static_assert(!answers<ex::inline_scheduler, error_scheduler_query, sched_env>);
static_assert(!answers<inline_attrs, stopped_scheduler_query, sched_env>);
static_assert(!answers<ex::inline_scheduler, stopped_scheduler_query, sched_env>);
static_assert(!answers<inline_attrs, value_scheduler_query, forge::empty_env>);
static_assert(!answers<ex::inline_scheduler, value_scheduler_query, forge::empty_env>);
static_assert(!answers<inline_attrs, value_scheduler_query, sched_env, sched_env>);
static_assert(!answers<ex::inline_scheduler, value_scheduler_query, sched_env, sched_env>);
static_assert(!answers<inline_attrs, value_domain_query>);
static_assert(!answers<ex::inline_scheduler, value_domain_query>);
static_assert(!answers<inline_attrs, error_domain_query, sched_env>);
static_assert(!answers<ex::inline_scheduler, stopped_domain_query, sched_env>);
static_assert(!answers<inline_attrs, missing>);
static_assert(!answers<ex::inline_scheduler, missing>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const inline_attrs&>(), std::declval<sched_env&>())), const loop_scheduler&>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const ex::inline_scheduler&>(), std::declval<sched_env&>())), const loop_scheduler&>);
static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
    std::declval<const inline_attrs&>(), std::declval<explicit_domain_env&>())), domain>);
static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
    std::declval<const ex::inline_scheduler&>(), std::declval<explicit_domain_env&>())), domain>);
// HIDE-SCHED terminates domain selection when an env contains an inline scheduler.
using inline_sched_env = ex::prop<ex::get_scheduler_t, ex::inline_scheduler>;
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const inline_sched_env&>())),
                           ex::default_domain>);
// Existing exact-environment domain customizations retain their compatible type.
struct exact_domain_env;
struct exact_domain_scheduler : ex::inline_scheduler {
    domain query(value_domain_query, const exact_domain_env&) const noexcept { return {}; }
    template<class Env>
    friend ex::default_domain tag_invoke(value_domain_query, exact_domain_scheduler,
                                         const Env&) noexcept { return {}; }
};
struct exact_domain_env {
    exact_domain_scheduler query(ex::get_scheduler_t) const noexcept { return {}; }
};
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const exact_domain_env&>())), domain>);
// An exact legacy overload must beat a generic fallback even when that
// fallback accepts HIDE-SCHED. The strict surface ignores both legacy hooks.
struct competing_domain_env;
struct competing_domain_scheduler {
    using scheduler_concept = ex::scheduler_t;
    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const competing_domain_scheduler&) const noexcept = default;
    template<class Env>
    friend ex::default_domain tag_invoke(value_domain_query, competing_domain_scheduler,
                                         const Env&) noexcept { return {}; }
};
struct competing_domain_env {
    competing_domain_scheduler query(ex::get_scheduler_t) const noexcept { return {}; }
    friend domain tag_invoke(value_domain_query, competing_domain_scheduler,
                             const competing_domain_env&) noexcept { return {}; }
};
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const competing_domain_env&>())),
                           ex::default_domain>);
#else
static_assert(std::same_as<decltype(value_domain_query{}(
    competing_domain_scheduler{}, std::declval<const competing_domain_env&>())), domain>);
static_assert(std::same_as<decltype(value_domain_query{}(
    competing_domain_scheduler{},
    std::declval<ex::__forge_domain::__hide_scheduler<competing_domain_env>>())), ex::default_domain>);
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const competing_domain_env&>())), domain>);
#endif
// The same competition must preserve the exact member without any ADL hook.
struct competing_member_domain_env;
struct competing_member_domain_scheduler {
    using scheduler_concept = ex::scheduler_t;
    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    bool operator==(const competing_member_domain_scheduler&) const noexcept = default;
    domain query(value_domain_query, const competing_member_domain_env&) const noexcept { return {}; }
    template<class Env>
    ex::default_domain query(value_domain_query, const Env&) const noexcept { return {}; }
};
struct competing_member_domain_env {
    competing_member_domain_scheduler query(ex::get_scheduler_t) const noexcept { return {}; }
};
static_assert(!ex::__forge_detail::tag_invocable<value_domain_query,
    competing_member_domain_scheduler, const competing_member_domain_env&>);
static_assert(std::same_as<decltype(value_domain_query{}(
    competing_member_domain_scheduler{}, std::declval<const competing_member_domain_env&>())), domain>);
static_assert(std::same_as<decltype(value_domain_query{}(competing_member_domain_scheduler{},
    std::declval<ex::__forge_domain::__hide_scheduler<competing_member_domain_env>>())),
    ex::default_domain>);
static_assert(std::same_as<decltype(ex::get_domain(
    std::declval<const competing_member_domain_env&>())), domain>);

// Recursion must also terminate when another scheduler inherits inline queries.
struct forwarding_inline_scheduler : ex::inline_scheduler {};
using forwarding_inline_env = ex::prop<ex::get_scheduler_t, forwarding_inline_scheduler>;
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const forwarding_inline_env&>())),
                           ex::default_domain>);
struct invalid_domain {
    invalid_domain() = delete;
};
struct invalid_domain_env {
    invalid_domain query(ex::get_domain_t) const noexcept;
};
// An invalid direct domain must not silently select a later default alternative.
static_assert(!cpo_answers<invalid_domain_env, ex::get_domain_t>);
static_assert(!answers<inline_attrs, value_domain_query, invalid_domain_env>);
static_assert(!answers<ex::inline_scheduler, value_domain_query, invalid_domain_env>);
// Inline attributes apply the draft's hidden scheduler lookup internally;
// the outer compatibility get_domain keeps the original overload set above.
static_assert(std::same_as<decltype(value_domain_query{}(inline_attrs{},
    std::declval<const competing_member_domain_env&>())), ex::default_domain>);
static_assert(std::same_as<decltype(value_domain_query{}(ex::inline_scheduler{},
    std::declval<const competing_member_domain_env&>())), ex::default_domain>);

// Check both completion CPOs against the same competing query surfaces.
// Results distinguish exact, generic, member, ADL and unary alternatives.
struct selection_env {};
template<class Q, class Exact, class Fallback>
struct competing_query_members {
    Exact query(Q, const selection_env&) const noexcept { return {}; }
    template<class Env>
    Fallback query(Q, const Env&) const noexcept(false) { return {}; }
    Fallback query(Q) const noexcept(false) { return {}; }
};
template<class Q, class Exact, class Fallback>
struct competing_query_adl {
    friend Exact tag_invoke(Q, const competing_query_adl&, const selection_env&) noexcept { return {}; }
    template<class Env>
    friend Fallback tag_invoke(Q, const competing_query_adl&, const Env&) noexcept(false) { return {}; }
    friend Fallback tag_invoke(Q, const competing_query_adl&) noexcept(false) { return {}; }
};
template<class Q, class Member, class Legacy>
struct generic_member_exact_adl {
    Member query(Q) const noexcept { return {}; }
    template<class Env>
    Member query(Q, const Env&) const noexcept { return {}; }
    friend Legacy tag_invoke(Q, const generic_member_exact_adl&) noexcept(false) { return {}; }
    friend Legacy tag_invoke(Q, const generic_member_exact_adl&, const selection_env&) noexcept(false) {
        return {};
    }
};
template<class Q, class Member, class Legacy>
struct exact_member_generic_adl {
    Member query(Q) const noexcept { return {}; }
    Member query(Q, const selection_env&) const noexcept { return {}; }
    friend Legacy tag_invoke(Q, const exact_member_generic_adl&) noexcept(false) { return {}; }
    template<class Env>
    friend Legacy tag_invoke(Q, const exact_member_generic_adl&, const Env&) noexcept(false) { return {}; }
};
template<class Q, class Unary, class Dependent>
struct unary_member_dependent_adl {
    Unary query(Q) const noexcept { return {}; }
    friend Dependent tag_invoke(Q, const unary_member_dependent_adl&,
                                const selection_env&) noexcept(false) { return {}; }
};
template<class Q, class Unary>
struct unary_query_member {
    Unary query(Q) const noexcept { return {}; }
};
template<class Q, class Exact, class Fallback>
constexpr bool completion_query_selection() {
    using members = competing_query_members<Q, Exact, Fallback>;
    using mixed = generic_member_exact_adl<Q, Exact, Fallback>;
    using exact_mixed = exact_member_generic_adl<Q, Exact, Fallback>;
    using unary = unary_query_member<Q, Exact>;
    return std::same_as<decltype(Q{}(members{}, selection_env{})), Exact> &&
           std::same_as<decltype(Q{}(members{}, forge::empty_env{})), Fallback> &&
           std::same_as<decltype(Q{}(members{})), Fallback> &&
           std::same_as<decltype(Q{}(mixed{}, selection_env{})), Exact> &&
           std::same_as<decltype(Q{}(mixed{})), Exact> &&
           std::same_as<decltype(Q{}(exact_mixed{}, selection_env{})), Exact> &&
           std::same_as<decltype(Q{}(exact_mixed{})), Exact> &&
           std::same_as<decltype(Q{}(unary{})), Exact> &&
           std::same_as<decltype(Q{}(unary{}, selection_env{})), Exact> &&
           std::same_as<decltype(Q{}(unary{}, selection_env{}, forge::empty_env{})), Exact>;
}
static_assert(completion_query_selection<value_scheduler_query, domain_scheduler, ex::inline_scheduler>());
static_assert(completion_query_selection<value_domain_query, domain, ex::default_domain>());
using direct_domain_mixed = generic_member_exact_adl<ex::get_domain_t, domain, ex::default_domain>;
static_assert(std::same_as<decltype(ex::get_domain(direct_domain_mixed{})), domain>);
using dependent_scheduler_adl = unary_member_dependent_adl<
    value_scheduler_query, domain_scheduler, ex::inline_scheduler>;
using dependent_domain_adl = unary_member_dependent_adl<
    value_domain_query, domain, ex::default_domain>;
using scheduler_adl_pair = competing_query_adl<
    value_scheduler_query, domain_scheduler, ex::inline_scheduler>;
using domain_adl_pair = competing_query_adl<value_domain_query, domain, ex::default_domain>;
static_assert(std::same_as<decltype(value_scheduler_query{}(dependent_scheduler_adl{})), domain_scheduler>);
static_assert(std::same_as<decltype(value_domain_query{}(dependent_domain_adl{})), domain>);
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(!cpo_answers<scheduler_adl_pair, value_scheduler_query>);
static_assert(!cpo_answers<scheduler_adl_pair, value_scheduler_query, selection_env>);
static_assert(!cpo_answers<domain_adl_pair, value_domain_query>);
static_assert(!cpo_answers<domain_adl_pair, value_domain_query, selection_env>);
static_assert(std::same_as<decltype(value_scheduler_query{}(
    dependent_scheduler_adl{}, selection_env{})), domain_scheduler>);
static_assert(std::same_as<decltype(value_domain_query{}(
    dependent_domain_adl{}, selection_env{})), domain>);
#else
static_assert(std::same_as<decltype(value_scheduler_query{}(scheduler_adl_pair{})), ex::inline_scheduler>);
static_assert(std::same_as<decltype(value_scheduler_query{}(scheduler_adl_pair{}, selection_env{})),
                           domain_scheduler>);
static_assert(std::same_as<decltype(value_scheduler_query{}(scheduler_adl_pair{}, forge::empty_env{})),
                           ex::inline_scheduler>);
static_assert(std::same_as<decltype(value_scheduler_query{}(
    scheduler_adl_pair{}, selection_env{}, forge::empty_env{})), ex::inline_scheduler>);
static_assert(std::same_as<decltype(value_domain_query{}(domain_adl_pair{})), ex::default_domain>);
static_assert(std::same_as<decltype(value_domain_query{}(domain_adl_pair{}, selection_env{})), domain>);
static_assert(std::same_as<decltype(value_domain_query{}(domain_adl_pair{}, forge::empty_env{})),
                           ex::default_domain>);
static_assert(std::same_as<decltype(value_domain_query{}(
    domain_adl_pair{}, selection_env{}, forge::empty_env{})), ex::default_domain>);
// A query with the supplied environment precedes the unary TRY-QUERY fallback,
// including when the environment-dependent hook is ADL and the unary is a member.
static_assert(std::same_as<decltype(value_scheduler_query{}(
    dependent_scheduler_adl{}, selection_env{})), ex::inline_scheduler>);
static_assert(!noexcept(value_scheduler_query{}(dependent_scheduler_adl{}, selection_env{})));
static_assert(std::same_as<decltype(value_domain_query{}(
    dependent_domain_adl{}, selection_env{})), ex::default_domain>);
#endif
using hidden_env = ex::__forge_domain::__hide_scheduler<answer>;
static_assert(!answers<hidden_env, ex::get_scheduler_t>);
static_assert(!answers<hidden_env, ex::get_domain_t>);
static_assert(nothrow_answer<hidden_env, key, const int&>);
static_assert(answers<hidden_env, local_key>);
static_assert(answers<hidden_env, variadic_key, int&, int>);

struct nothrow_receiver {
    using receiver_concept = ex::receiver_t;
    void set_value() && noexcept {}
    void set_stopped() && noexcept {}
    forge::empty_env get_env() const noexcept { return {}; }
};
struct throwing_copy_receiver : nothrow_receiver {
    throwing_copy_receiver() = default;
    throwing_copy_receiver(throwing_copy_receiver&&) noexcept = default;
    throwing_copy_receiver(const throwing_copy_receiver& other) noexcept(false)
        : nothrow_receiver(other) {}
};
static_assert(ex::receiver<nothrow_receiver>);
static_assert(ex::receiver<throwing_copy_receiver>);
static_assert(ex::receiver<throwing_copy_receiver&>);
static_assert(ex::receiver<const throwing_copy_receiver&>);
static_assert(ex::receiver_of<throwing_copy_receiver,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
using inline_sender = decltype(ex::schedule(ex::inline_scheduler{}));
using loop_sender = decltype(ex::schedule(std::declval<loop_scheduler>()));
template<class S>
constexpr bool schedule_noexcept =
    noexcept(ex::connect(std::declval<S>(), nothrow_receiver{})) &&
    noexcept(ex::connect(std::declval<const S&>(), nothrow_receiver{})) &&
    noexcept(ex::connect(std::declval<S>(), std::declval<throwing_copy_receiver>())) &&
    noexcept(ex::connect(std::declval<const S&>(), std::declval<throwing_copy_receiver>())) &&
    !noexcept(ex::connect(std::declval<S>(), std::declval<throwing_copy_receiver&>())) &&
    !noexcept(ex::connect(std::declval<const S&>(), std::declval<const throwing_copy_receiver&>())) &&
    noexcept(ex::start(std::declval<ex::connect_result_t<S, nothrow_receiver>&>())) &&
    noexcept(ex::start(std::declval<ex::connect_result_t<const S&, throwing_copy_receiver&>&>()));
static_assert(schedule_noexcept<inline_sender>);
static_assert(schedule_noexcept<loop_sender>);
using written_inline = decltype(ex::write_env(std::declval<inline_sender>(), forge::empty_env{}));
static_assert(schedule_noexcept<written_inline>);
using written_loop = decltype(ex::write_env(std::declval<loop_sender>(), forge::empty_env{}));
static_assert(schedule_noexcept<written_loop>);

using missing_scheduler_let_env = ex::__forge_let::__let_env_t<
    ex::__forge_let::__value_tag, inline_sender, explicit_domain_env>;
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
static_assert(std::same_as<missing_scheduler_let_env, ex::prop<ex::get_domain_t, domain>>);
static_assert(!answers<missing_scheduler_let_env, ex::get_start_scheduler_t>);
#else
static_assert(std::same_as<missing_scheduler_let_env,
                           ex::__forge_let::__scheduler_env<ex::inline_scheduler>>);
#endif
static_assert(noexcept(ex::get_env(std::declval<const inline_sender&>())));
static_assert(noexcept(ex::get_env(std::declval<const loop_sender&>())));
static_assert(noexcept(ex::schedule(std::declval<const ex::inline_scheduler&>())));
static_assert(noexcept(ex::schedule(std::declval<const loop_scheduler&>())));
static_assert(std::same_as<ex::completion_signatures_of_t<inline_sender, forge::empty_env>,
                           ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<loop_sender, forge::empty_env>,
                           ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<loop_sender, stop_prop>,
                           ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);

#if !defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) || !FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
// continues_on needs completion-domain filtering before variadic member/strict coverage.
using attrs = ex::__forge_continues_on::__completion_attrs<ex::inline_scheduler, answer>;
static_assert(ex::__forge_env_detail::__queryable<key, const attrs&>);
static_assert(!ex::__forge_env_detail::__queryable<local_key, const attrs&>);
static_assert(!ex::__forge_env_detail::__queryable<ex::get_completion_scheduler_t<ex::set_error_t>, const attrs&>);
using domain_attrs = ex::__forge_continues_on::__completion_attrs<domain_scheduler, answer>;
static_assert(ex::__forge_env_detail::__queryable<
    ex::get_completion_domain_t<ex::set_value_t>, const domain_attrs&, const answer&>);
#endif
} // namespace

int main() {}

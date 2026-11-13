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
// Defer spawn_future members/strict dispatch until stop-when/write_env apply FWD-ENV.
static_assert(!answers<spawn_env, ex::get_stop_token_t>);
static_assert(!answers<spawn_env, key>);
static_assert(!answers<spawn_env, local_key>);
static_assert(!answers<spawn_env, missing>);
static_assert(!answers<spawn_env, ex::get_stop_token_t, int>);
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

#if !defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) || !FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
// spawn_future needs stop-when/write_env FWD-ENV filtering before member/strict coverage.
static_assert(ex::__forge_env_detail::__queryable<key, const spawn_env&>);
static_assert(std::same_as<decltype(ex::get_stop_token(std::declval<const spawn_env&>())),
                           std::inplace_stop_token>);
// run_loop stays out of strict coverage until scheduler queries match schedule attrs [exec.sched].
using loop_env = ex::__forge_run_loop::__env;
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const loop_env&>())), ex::run_loop::scheduler>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_stopped_t>(
    std::declval<const loop_env&>())), ex::run_loop::scheduler>);
// inline_scheduler needs env-dependent inline-attrs equivalence before strict coverage.
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_value_t>(
    std::declval<const ex::__forge_inline::env&>())), ex::inline_scheduler>);
// write_env needs FWD-ENV filtering of the base before member/strict coverage.
using joined_env = ex::__forge_write_env::__joined_env<answer, throwing_answer>;
static_assert(ex::__forge_env_detail::__queryable<key, const joined_env&>);
// counting_scope needs stop-when's FWD-ENV filtering before member/strict coverage.
using scope_env = ex::__forge_counting_scope::__stop_env<answer>;
static_assert(ex::__forge_env_detail::__queryable<key, const scope_env&>);
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

// Independent type and expression oracles from [exec.queries], [exec.sched],
// [exec.cmplsig], [exec.envs] and [exec.domain.indeterminate].
#include <forge/execution_extensions.hpp>
#include "cpp26/execution.hpp"
#include <memory>

namespace ex = std::execution;

template<class... Ts> struct types {};
template<class T> using one = types<T>;
template<class T, class U> using two = types<T, U>;
template<class... Ts> using values = types<Ts...>;

template<class... Es>
concept can_env = requires { typename ex::env<Es...>; };
struct bad_destructor { ~bad_destructor() noexcept(false); };
static_assert(can_env<int> && !can_env<bad_destructor>);
static_assert(std::same_as<ex::env_of_t<int>, ex::env<>>);
static_assert(std::same_as<ex::env_of_t<decltype(ex::just())>, ex::env<>>);
static_assert(std::same_as<forge::empty_env, ex::env<>>);
static_assert(!std::is_copy_assignable_v<ex::env<>>);
static_assert(!std::is_move_assignable_v<ex::env<>>);
static_assert(!std::is_copy_assignable_v<ex::env<int&>>);
static_assert(!std::is_move_assignable_v<ex::env<int&>>);
using stop_prop = ex::prop<std::get_stop_token_t, std::never_stop_token>;
static_assert(!std::is_copy_assignable_v<stop_prop>);
static_assert(!std::is_move_assignable_v<stop_prop>);
static_assert(std::is_aggregate_v<stop_prop>);
static_assert(std::copy_constructible<stop_prop>);
constexpr stop_prop default_stop_prop{};
static_assert(!std::get_stop_token(default_stop_prop).stop_possible());
struct user_query {
    template<class E>
    constexpr decltype(auto) operator()(const E& e) const noexcept { return e.query(*this); }
};
// Const tag storage alone would still permit both assignments for this tag.
struct const_assignable_query {
    constexpr const_assignable_query() = default;
    const_assignable_query(const const_assignable_query&) = default;
    const_assignable_query(const_assignable_query&&) = default;
    constexpr const const_assignable_query& operator=(const const_assignable_query&) const noexcept {
        return *this;
    }
    constexpr const const_assignable_query& operator=(const_assignable_query&&) const noexcept {
        return *this;
    }
    template<class E>
    constexpr decltype(auto) operator()(const E& e) const noexcept { return e.query(*this); }
};
static_assert(std::is_assignable_v<const const_assignable_query&, const const_assignable_query&>);
static_assert(std::is_assignable_v<const const_assignable_query&, const_assignable_query&&>);
using const_assignable_prop = ex::prop<const_assignable_query, int>;
static_assert(std::is_aggregate_v<const_assignable_prop>);
static_assert(std::copy_constructible<const_assignable_prop>);
static_assert(std::move_constructible<const_assignable_prop>);
static_assert(!std::is_copy_assignable_v<const_assignable_prop>);
static_assert(!std::is_move_assignable_v<const_assignable_prop>);
constexpr auto const_assignable_paren_prop = ex::prop(const_assignable_query{}, 31);
constexpr auto const_assignable_brace_prop = ex::prop{const_assignable_query{}, 32};
static_assert(std::same_as<std::remove_cv_t<decltype(const_assignable_paren_prop)>, const_assignable_prop>);
static_assert(std::same_as<std::remove_cv_t<decltype(const_assignable_brace_prop)>, const_assignable_prop>);
static_assert(const_assignable_query{}(const_assignable_paren_prop) == 31);
static_assert(const_assignable_query{}(const_assignable_brace_prop) == 32);
static_assert(sizeof(ex::prop<user_query, int>) == sizeof(int));
static_assert(sizeof(const_assignable_prop) == sizeof(int));
using move_prop = ex::prop<user_query, std::unique_ptr<int>>;
static_assert(std::move_constructible<move_prop> && !std::copy_constructible<move_prop>);
static_assert(!std::is_copy_assignable_v<move_prop>);
static_assert(!std::is_move_assignable_v<move_prop>);
static_assert(std::move_constructible<ex::env<move_prop>>);
// Both forms use the WD's two-argument deduction guide, including unwrapping.
template<class Q, class V>
constexpr auto paren_prop(Q q, V&& value) {
    return ex::prop(q, static_cast<V&&>(value));
}
template<class Q, class V>
constexpr auto brace_prop(Q q, V&& value) {
    return ex::prop{q, static_cast<V&&>(value)};
}
// Nondependent CTAD inside a function template must also keep two elements.
template<class>
constexpr auto fixed_paren_prop() {
    return ex::prop(std::get_stop_token_t{}, std::never_stop_token{});
}
template<class>
constexpr auto fixed_brace_prop() {
    return ex::prop{std::get_stop_token_t{}, std::never_stop_token{}};
}
static_assert(std::same_as<decltype(fixed_paren_prop<int>()), stop_prop>);
static_assert(std::same_as<decltype(fixed_brace_prop<int>()), stop_prop>);
static_assert(!std::get_stop_token(fixed_paren_prop<int>()).stop_possible());
static_assert(!std::get_stop_token(fixed_brace_prop<int>()).stop_possible());
constexpr bool two_element_prop() {
    auto [tag_holder, token] = fixed_paren_prop<int>();
    (void)tag_holder;
    return !token.stop_possible();
}
static_assert(two_element_prop());
static_assert(std::same_as<decltype(ex::prop(std::get_stop_token, std::never_stop_token{})), stop_prop>);
static_assert(std::same_as<decltype(ex::prop{std::get_stop_token, std::never_stop_token{}}), stop_prop>);
static_assert(std::same_as<decltype(paren_prop(std::get_stop_token, std::never_stop_token{})), stop_prop>);
static_assert(std::same_as<decltype(brace_prop(std::get_stop_token, std::never_stop_token{})), stop_prop>);
static_assert(!std::get_stop_token(paren_prop(std::get_stop_token, std::never_stop_token{})).stop_possible());
static_assert(!std::get_stop_token(brace_prop(std::get_stop_token, std::never_stop_token{})).stop_possible());
static_assert(std::same_as<decltype(paren_prop(user_query{}, std::unique_ptr<int>{})), move_prop>);
static_assert(std::same_as<decltype(brace_prop(user_query{}, std::unique_ptr<int>{})), move_prop>);
using borrowed_prop = ex::prop<user_query, int&>;
static_assert(std::same_as<decltype(paren_prop(user_query{}, std::ref(std::declval<int&>()))), borrowed_prop>);
static_assert(std::same_as<decltype(brace_prop(user_query{}, std::ref(std::declval<int&>()))), borrowed_prop>);
struct deduction_env {
    int value;
    constexpr int query(user_query) const noexcept { return value; }
};
template<class>
constexpr auto fixed_borrowed_env(deduction_env& parent) {
    return ex::env{std::ref(parent)};
}
constexpr bool env_deduction_controls() {
    deduction_env parent{1};
    const deduction_env const_parent{2};
    auto borrowed = ex::env{std::ref(parent)};
    auto const_borrowed = ex::env{std::cref(const_parent)};
    auto mixed = ex::env{deduction_env{3}, std::ref(parent)};
    auto from_template = fixed_borrowed_env<int>(parent);
    auto copied = ex::env{borrowed};
    static_assert(std::same_as<decltype(borrowed), ex::env<deduction_env&>>);
    static_assert(std::same_as<decltype(const_borrowed), ex::env<const deduction_env&>>);
    static_assert(std::same_as<decltype(mixed), ex::env<deduction_env, deduction_env&>>);
    static_assert(std::same_as<decltype(from_template), ex::env<deduction_env&>>);
    static_assert(std::same_as<decltype(copied), ex::env<deduction_env&>>);
    parent.value = 4;
    return user_query{}(borrowed) == 4 && user_query{}(const_borrowed) == 2 &&
        user_query{}(mixed) == 3 && user_query{}(from_template) == 4 && user_query{}(copied) == 4;
}
static_assert(env_deduction_controls());
// Instantiate the nondependent aggregate expression inside the adaptor template.
static_assert(ex::sender_in<decltype(ex::unstoppable(ex::just())), ex::env<>>);
static_assert(&std::get_allocator == &ex::get_allocator);
static_assert(&std::get_stop_token == &ex::get_stop_token);
static_assert(&std::forwarding_query == &ex::forwarding_query);
static_assert(std::same_as<std::stop_token_of_t<stop_prop>, std::never_stop_token>);
constexpr bool query_constants() {
    using namespace std;
    using namespace std::execution;
    auto e = env{prop{std::get_allocator, std::allocator<int>{}},
                 prop{get_scheduler, inline_scheduler{}},
                 prop{get_start_scheduler, inline_scheduler{}},
                 prop{get_delegation_scheduler, inline_scheduler{}}};
    return !get_stop_token(e).stop_possible() && get_scheduler(e) == inline_scheduler{} &&
        get_start_scheduler(e) == inline_scheduler{} && get_delegation_scheduler(e) == inline_scheduler{} &&
        get_allocator(e) == std::allocator<int>{};
}
static_assert(query_constants());
static_assert(std::same_as<decltype(ex::get_domain(ex::env<>{})), ex::default_domain>);
constexpr auto empty_domain = ex::get_domain(ex::env<>{});
constexpr auto read_token = ex::read_env(std::get_stop_token);
constexpr auto written_token = ex::write_env(read_token, ex::env<>{});
static_assert(ex::sender_in<decltype(read_token), ex::env<>>);
static_assert(!ex::sender_in<decltype(ex::read_env(std::get_allocator)), ex::env<>>);
struct void_query { void operator()(ex::env<>&) const noexcept; };
struct rvalue_query { int operator()(ex::env<>&&) const noexcept; };
struct nondefault_query {
    nondefault_query() = delete;
    explicit nondefault_query(int);
    int operator()(ex::env<>&) const noexcept;
};
struct reference_query { int& operator()(const ex::env<>&) const noexcept; };
struct throwing_query { int operator()(const ex::env<>&) const; };
static_assert(!ex::sender_in<decltype(ex::read_env(void_query{})), ex::env<>>);
static_assert(!ex::sender_in<decltype(ex::read_env(rvalue_query{})), ex::env<>>);
static_assert(!ex::sender_in<decltype(ex::read_env(nondefault_query{1})), ex::env<>>);
static_assert(std::same_as<ex::completion_signatures_of_t<decltype(ex::read_env(reference_query{})), ex::env<>>,
                           ex::completion_signatures<ex::set_value_t(int&)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<decltype(ex::read_env(throwing_query{})), ex::env<>>,
                           ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);

// Q()(Env&) validates participation; the stored Q& determines execution.
struct category_query {
    long operator()(ex::env<>&) && noexcept;
    int operator()(ex::env<>&&) &;
    short operator()(ex::env<>&) & noexcept;
};
struct reverse_category_query {
    long operator()(ex::env<>&) &&;
    int operator()(ex::env<>&&) & noexcept;
};
using category_sender = decltype(ex::read_env(category_query{}));
static_assert(std::same_as<ex::completion_signatures_of_t<category_sender, ex::env<>>,
    ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<category_sender, ex::env<>&>,
    ex::completion_signatures<ex::set_value_t(short)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<
    decltype(ex::read_env(reverse_category_query{})), ex::env<>>,
    ex::completion_signatures<ex::set_value_t(int)>>);

struct immovable_environment {
    immovable_environment() = default;
    immovable_environment(const immovable_environment&) = delete;
    immovable_environment(immovable_environment&&) = delete;
};
struct elided_query {
    long operator()(const immovable_environment&) && noexcept;
    int operator()(immovable_environment) & noexcept;
};
static_assert(std::same_as<ex::completion_signatures_of_t<
    decltype(ex::read_env(elided_query{})), immovable_environment>,
    ex::completion_signatures<ex::set_value_t(int)>>);
struct member_environment { int value; };
static_assert(!ex::sender_in<decltype(ex::read_env(&member_environment::value)), member_environment>);

struct receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value() const noexcept;
    void set_error(int) const noexcept;
    void set_stopped() const noexcept;
};
static_assert(std::invocable<ex::set_value_t, receiver>);
static_assert(!std::invocable<ex::set_value_t, receiver&>);
static_assert(!std::invocable<ex::set_value_t, const receiver>);
static_assert(std::invocable<ex::set_error_t, receiver, int>);
static_assert(!std::invocable<ex::set_error_t, receiver&, int>);
static_assert(!std::invocable<ex::set_error_t, const receiver, int>);
static_assert(std::invocable<ex::set_stopped_t, receiver>);
static_assert(!std::invocable<ex::set_stopped_t, receiver&>);
static_assert(!std::invocable<ex::set_stopped_t, const receiver>);
struct incomplete;
struct reconstructible : receiver {
    static reconstructible make_receiver_for(incomplete*) noexcept;
};
struct wrong_reconstructible : receiver {
    static receiver make_receiver_for(incomplete*) noexcept;
};
struct member_reconstructible : receiver {
    member_reconstructible make_receiver_for(incomplete*) noexcept;
};
static_assert(!ex::inlinable_receiver<wrong_reconstructible, incomplete>);
static_assert(!ex::inlinable_receiver<member_reconstructible, incomplete>);
struct throwing_reconstructible : receiver {
    static throwing_reconstructible make_receiver_for(incomplete*);
};
static_assert(ex::inlinable_receiver<reconstructible, incomplete>);
static_assert(!ex::inlinable_receiver<receiver, incomplete>);
static_assert(!ex::inlinable_receiver<throwing_reconstructible, incomplete>);
struct operation { void start() const noexcept; };
static_assert(std::invocable<ex::start_t, operation&>);
static_assert(!std::invocable<ex::start_t, operation>);
static_assert(!std::invocable<ex::start_t, const operation>);

struct scheduler {
    using scheduler_concept = ex::scheduler_tag;
    constexpr auto schedule() const noexcept { return ex::inline_scheduler{}.schedule(); }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const scheduler&) const noexcept = default;
};
struct lvalue_scheduler : scheduler { auto schedule() & noexcept { return ex::just(); } };
struct nonassignable_scheduler : scheduler {
    nonassignable_scheduler& operator=(const nonassignable_scheduler&) = delete;
};
struct nonsender_scheduler : scheduler { int schedule() const noexcept; };
struct invalid_scheduler_env { int query(ex::get_scheduler_t) const noexcept; };
static_assert(!std::invocable<ex::get_scheduler_t, ex::env<>>);
static_assert(!std::invocable<ex::get_scheduler_t, invalid_scheduler_env>);
struct non_scheduler_progress {
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept;
};
static_assert(!ex::scheduler<ex::env<>> && !ex::scheduler<int>);
static_assert(!ex::scheduler<decltype(ex::just())>);
static_assert(!std::invocable<ex::schedule_t, ex::env<>> && !std::invocable<ex::schedule_t, int>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, ex::env<>, ex::env<>>);
static_assert(ex::scheduler<scheduler>);
static_assert(!ex::scheduler<lvalue_scheduler> && ex::scheduler<lvalue_scheduler&>);
static_assert(!ex::scheduler<const lvalue_scheduler&>);
static_assert(!ex::scheduler<nonassignable_scheduler>);
static_assert(!ex::scheduler<nonsender_scheduler>);
static_assert(!std::invocable<ex::get_forward_progress_guarantee_t, non_scheduler_progress>);
template<class S> concept schedule_result = requires { typename ex::schedule_result_t<S>; };
static_assert(!schedule_result<int> && !schedule_result<lvalue_scheduler>);
static_assert(schedule_result<lvalue_scheduler&>);
static_assert(std::same_as<ex::schedule_result_t<ex::inline_scheduler>, decltype(ex::schedule(ex::inline_scheduler{}))>);
static_assert(std::same_as<ex::schedule_result_t<ex::run_loop::scheduler>, decltype(ex::schedule(std::declval<ex::run_loop::scheduler>()))>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<int>, scheduler, ex::env<>>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<int>, scheduler>);
static_assert(std::same_as<decltype(ex::get_completion_scheduler<ex::set_error_t>(scheduler{}, ex::env<>{})), scheduler>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, ex::inline_scheduler>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, ex::__forge_inline::env>);

static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, lvalue_scheduler&>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, lvalue_scheduler, ex::env<>>);
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, const lvalue_scheduler&, ex::env<>>);
static_assert(std::invocable<ex::get_completion_scheduler_t<ex::set_value_t>, scheduler&, ex::env<>>);

struct derived_query : std::forwarding_query_t {
    derived_query() = delete;
    explicit derived_query(int);
};
struct query_environment { int query(derived_query) const noexcept; };
template<class E> concept answers_derived = requires(const E& e, derived_query q) { e.query(q); };
static_assert(answers_derived<ex::__forge_env_detail::__forwarding_attrs<query_environment>>);
static_assert(answers_derived<ex::__forge_let::__forwarding_env<query_environment>>);
static_assert(!ex::__forge_env_detail::__forwarding_query<derived_query>);

struct legacy_false_query : std::forwarding_query_t {
    friend constexpr bool tag_invoke(std::forwarding_query_t, legacy_false_query) noexcept { return false; }
};
struct member_false_query : std::forwarding_query_t {
    constexpr bool query(std::forwarding_query_t) const noexcept { return false; }
};
struct nondefault_false_query : std::forwarding_query_t {
    nondefault_false_query() = delete;
    explicit constexpr nondefault_false_query(int) {}
    friend constexpr bool tag_invoke(std::forwarding_query_t, nondefault_false_query) noexcept { return false; }
};
struct false_query_environment {
    int query(legacy_false_query) const noexcept;
    int query(member_false_query) const noexcept;
    int query(nondefault_false_query) const noexcept;
};
template<class E, class Q> concept answers_query = requires(const E& e, Q q) { e.query(q); };
static_assert(!std::forwarding_query(legacy_false_query{}));
static_assert(!std::forwarding_query(member_false_query{}));
static_assert(!answers_query<ex::__forge_env_detail::__forwarding_attrs<false_query_environment>, legacy_false_query>);
static_assert(!answers_query<ex::__forge_env_detail::__forwarding_attrs<false_query_environment>, member_false_query>);
static_assert(!answers_query<ex::__forge_let::__forwarding_env<false_query_environment>, legacy_false_query>);
static_assert(!answers_query<ex::__forge_let::__forwarding_env<false_query_environment>, member_false_query>);

static_assert(!answers_query<ex::__forge_env_detail::__forwarding_attrs<false_query_environment>, nondefault_false_query>);
static_assert(!answers_query<ex::__forge_let::__forwarding_env<false_query_environment>, nondefault_false_query>);

struct domain_a {};
struct domain_b {};
using indeterminate = ex::indeterminate_domain<domain_a, domain_b>;
static_assert(std::same_as<std::common_type_t<ex::indeterminate_domain<domain_a>,
    ex::indeterminate_domain<domain_b, domain_a>>, indeterminate>);
static_assert(std::same_as<std::common_type_t<ex::indeterminate_domain<>, domain_a>, domain_a>);
static_assert(std::same_as<std::common_type_t<domain_a, ex::indeterminate_domain<>>, domain_a>);
static_assert(std::same_as<std::common_type_t<indeterminate, domain_a>, indeterminate>);
static_assert(std::same_as<std::common_type_t<domain_a, indeterminate>, indeterminate>);
using reverse_indeterminate = std::common_type_t<ex::indeterminate_domain<domain_b, domain_a>,
    ex::indeterminate_domain<domain_a>>;
template<class T, class D> struct contains_domain;
template<class... Ds, class D>
struct contains_domain<ex::indeterminate_domain<Ds...>, D>
    : std::bool_constant<(std::same_as<Ds, D> || ...)> {};
template<class T> struct domain_count;
template<class... Ds> struct domain_count<ex::indeterminate_domain<Ds...>>
    : std::integral_constant<std::size_t, sizeof...(Ds)> {};
static_assert(contains_domain<indeterminate, domain_a>::value && contains_domain<indeterminate, domain_b>::value);
static_assert(contains_domain<reverse_indeterminate, domain_a>::value && contains_domain<reverse_indeterminate, domain_b>::value);
static_assert(domain_count<indeterminate>::value == 2 && domain_count<reverse_indeterminate>::value == 2);
struct nondefault_common { nondefault_common() = delete; };
static_assert(std::same_as<std::common_type_t<nondefault_common, nondefault_common>, nondefault_common>);
static_assert(std::same_as<ex::__forge_domain::__common_domain_t<nondefault_common, nondefault_common>,
    ex::indeterminate_domain<nondefault_common>>);
static_assert(std::same_as<ex::__forge_domain::__common_domain_t<domain_a, domain_a>, domain_a>);
using just = decltype(ex::just(1));
static_assert(std::same_as<decltype(indeterminate::transform_sender(ex::start, std::declval<just&>(), ex::env<>{})), just&>);
static_assert(std::same_as<decltype(reverse_indeterminate::transform_sender(
    ex::start, std::declval<just&>(), ex::env<>{})), just&>);
template<class S, class E> concept default_transform = requires(S&& s, const E& e) {
    ex::default_domain::transform_sender(ex::start, static_cast<S&&>(s), e);
};
static_assert(default_transform<just&, ex::env<>>);
struct nondefault_transform_tag {
    nondefault_transform_tag() = delete;
    explicit nondefault_transform_tag(int);
};
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    std::declval<nondefault_transform_tag>(), std::declval<just&>(), ex::env<>{})), just&>);
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    ex::start, std::declval<just&>(), ex::env<>{})), just&>);
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    ex::start, std::declval<const just&>(), ex::env<>{})), const just&>);
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    ex::start, std::declval<just>(), ex::env<>{})), just>);
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    std::declval<just&>(), ex::env<>{})), just&>);
static_assert(!default_transform<int, ex::env<>> && !default_transform<just&, bad_destructor>);
static_assert(std::same_as<decltype(ex::transform_sender<just&, ex::env<>>(std::declval<just&>(), ex::env<>{})), just&>);
template<class S, class E> decltype(auto) adl_transform(S&& s, const E& e) {
    return transform_sender(static_cast<S&&>(s), e);
}
static_assert(std::same_as<decltype(adl_transform(std::declval<just&>(), ex::env<>{})), just&>);
struct apply_tag {
    template<class S> constexpr int apply_sender(S&&, int v) const noexcept { return v; }
};
static_assert(std::same_as<decltype(ex::apply_sender<ex::default_domain, apply_tag, just>(
    ex::default_domain{}, apply_tag{}, std::declval<just>(), 7)), int>);

template<class... Sigs> concept signatures = requires { typename ex::completion_signatures<Sigs...>; };
static_assert(signatures<ex::set_value_t(int&, const int), ex::set_error_t(int), ex::set_stopped_t()>);
static_assert(!signatures<int> && !signatures<void()> && !signatures<ex::set_error_t()>);
static_assert(!signatures<ex::set_stopped_t(int)> && !signatures<ex::set_value_t() noexcept>);
struct invalid_signatures {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env> static int get_completion_signatures();
};
static_assert(!ex::sender_in<invalid_signatures, ex::env<>>);
template<class S> concept completion_alias = requires { typename ex::completion_signatures_of_t<S>; };
static_assert(!completion_alias<int> && !completion_alias<invalid_signatures>);
struct invalid_instance_signatures {
    using sender_concept = ex::sender_tag;
    int get_completion_signatures(ex::env<>) const;
};
struct invalid_legacy_signatures {
    using sender_concept = ex::sender_tag;
    friend int tag_invoke(ex::get_completion_signatures_t, invalid_legacy_signatures, ex::env<>);
};
static_assert(!ex::sender_in<invalid_instance_signatures, ex::env<>>);
static_assert(!ex::sender_in<invalid_legacy_signatures, ex::env<>>);
struct signature_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() -> ex::completion_signatures<
        ex::set_value_t(int&), ex::set_value_t(const long&), ex::set_error_t(int), ex::set_stopped_t()>;
};
static_assert(std::same_as<ex::value_types_of_t<signature_sender, ex::env<>, one, two>,
                           types<types<int&>, types<const long&>>>);
static_assert(std::same_as<ex::error_types_of_t<signature_sender, ex::env<>, one>, types<int>>);
static_assert(ex::sends_stopped<signature_sender>);
struct single_signature_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() -> ex::completion_signatures<
        ex::set_value_t(int&), ex::set_error_t(long&)>;
};
static_assert(std::same_as<ex::value_types_of_t<single_signature_sender, ex::env<>,
    std::tuple, std::type_identity_t>, std::tuple<int&>>);
static_assert(std::same_as<ex::error_types_of_t<single_signature_sender, ex::env<>,
    std::type_identity_t>, long&>);

template<class S, class E> concept public_transform = requires(S&& s, const E& e) {
    ex::transform_sender(static_cast<S&&>(s), e);
};
static_assert(public_transform<just&, ex::env<>>);
static_assert(!public_transform<int, ex::env<>> && !public_transform<just&, bad_destructor>);
template<class S, class Tag> concept public_apply = requires(S&& s) {
    ex::apply_sender(ex::default_domain{}, Tag{}, static_cast<S&&>(s), 1);
};
static_assert(public_apply<just, apply_tag>);
static_assert(!public_apply<int, apply_tag> && !public_apply<just, domain_a>);
static_assert(!std::same_as<ex::let_value_t, ex::let_error_t> &&
              !std::same_as<ex::let_value_t, ex::let_stopped_t> &&
              !std::same_as<ex::let_error_t, ex::let_stopped_t>);
static_assert(!std::same_as<ex::bulk_t, ex::bulk_chunked_t> &&
              !std::same_as<ex::bulk_t, ex::bulk_unchunked_t> &&
              !std::same_as<ex::bulk_chunked_t, ex::bulk_unchunked_t>);

#define FORGE_CHECK_TAG(name) static_assert(std::same_as<ex::name##_t, std::remove_cv_t<decltype(ex::name)>>)
FORGE_CHECK_TAG(then);
FORGE_CHECK_TAG(upon_error);
FORGE_CHECK_TAG(upon_stopped);
FORGE_CHECK_TAG(let_value);
FORGE_CHECK_TAG(let_error);
FORGE_CHECK_TAG(let_stopped);
FORGE_CHECK_TAG(bulk);
FORGE_CHECK_TAG(bulk_chunked);
FORGE_CHECK_TAG(bulk_unchunked);
FORGE_CHECK_TAG(on);
FORGE_CHECK_TAG(into_variant);
FORGE_CHECK_TAG(stopped_as_optional);
FORGE_CHECK_TAG(stopped_as_error);
FORGE_CHECK_TAG(spawn);
FORGE_CHECK_TAG(associate);
#undef FORGE_CHECK_TAG
int main() {}

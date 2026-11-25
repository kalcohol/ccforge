// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "detail.hpp"
#include "stop_token.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std {

struct forwarding_query_t {
    template<class Query>
    constexpr bool operator()(Query&& query) const noexcept;
};

inline constexpr forwarding_query_t forwarding_query{};

template<class Query>
constexpr bool forwarding_query_t::operator()(Query&& query) const noexcept {
    if constexpr (requires { static_cast<Query&&>(query).query(std::forwarding_query); }) {
        static_assert(noexcept(static_cast<Query&&>(query).query(std::forwarding_query)),
                      "forwarding query must be noexcept");
        static_assert(std::same_as<decltype(static_cast<Query&&>(query).query(std::forwarding_query)), bool>,
                      "forwarding query must return bool");
        return static_cast<Query&&>(query).query(std::forwarding_query);
    } else if constexpr (std::derived_from<std::remove_cvref_t<Query>, forwarding_query_t>) {
        return true;
    } else {
        return false;
    }
}

} // namespace std

namespace std::execution {

template<class T>
concept __forge_queryable = std::destructible<T>;

template<__forge_queryable... Envs>
struct env;
using __empty_env = env<>;
struct set_value_t;
struct set_error_t;
struct set_stopped_t;
struct get_domain_t;

namespace __forge_env_detail {
template<class S>
consteval bool __scheduler_shape();
template<class S>
consteval bool __scheduler();
template<class Env>
consteval bool __composed_scheduler_query();
} // namespace __forge_env_detail
namespace __forge_env_detail {

template<class T>
concept __member_get_env = requires(const T& obj) {
    obj.get_env();
};

template<class T>
concept __nothrow_member_get_env = requires(const T& obj) {
    { obj.get_env() } noexcept;
};

template<class Query, class Env, class... Args>
concept __member_query = requires(
    const std::remove_reference_t<Env>& env,
    const Query& query,
    Args&&... args) {
    env.query(query, static_cast<Args&&>(args)...);
};

template<class Query, class Env, class... Args>
concept __nothrow_member_query = requires(
    const std::remove_reference_t<Env>& env,
    const Query& query,
    Args&&... args) {
    { env.query(query, static_cast<Args&&>(args)...) } noexcept;
};

template<class Query, class Env, class... Args>
concept __queryable = __member_query<Query, Env, Args...>;

template<class Query, class Env, class... Args>
inline constexpr bool __nothrow_query =
    __nothrow_member_query<Query, Env, Args...>;

template<class Query, class Env, class... Args>
    requires __queryable<Query, Env, Args...>
constexpr decltype(auto) __query(const Query& query, Env&& env, Args&&... args)
    noexcept(__nothrow_query<Query, Env, Args...>) {
    return static_cast<const std::remove_reference_t<Env>&>(env)
        .query(query, static_cast<Args&&>(args)...);
}

// The locked when_all and counting_scope environments hand over a tag that
// they took by value. As at 8de815c, the member then sees that named, mutable
// tag; batch E replaces those callers.
template<class Query, class Env, class... Args>
    requires (!std::is_reference_v<Query>) && (!std::is_const_v<Query>) &&
             __queryable<Query, Env, Args...>
constexpr decltype(auto) __query(Query&& query, Env&& env, Args&&... args)
    noexcept(__nothrow_query<Query, Env, Args...>) {
    return static_cast<const std::remove_reference_t<Env>&>(env)
        .query(query, static_cast<Args&&>(args)...);
}

template<class Query>
concept __forwarding_query = requires {
    // Keep non-constant classification in the type-substitution failure path.
    typename std::enable_if_t<
        std::forwarding_query(std::remove_cvref_t<Query>{})>;
};

// The forwarding-query concept of [execution.syn], which [exec.let]/8.2 applies
// to the type of q. A deleted or missing default constructor and a constant
// false leave it unsatisfied. A well-formed forwarding_query(Q{}) that is not a
// constant expression is ill-formed, as for any atomic constraint.
template<class Query>
concept __forwarding_query_type = std::forwarding_query(Query{});

// FWD-ENV classifies the supplied query object with its own qualification
// ([exec.snd.expos]/4, [exec.fwd.env]/2). Participation is decided from the
// type, where the object's value is unavailable, so a classifier member is
// evaluated on a stand-in instance that carries the supplied qualification.
// An empty trivially copyable type has no state to read and needs no
// constructor. Any other type is value-initialized, which gives the answer of
// every object whose classification is an invariant of its type.
template<class Query>
concept __fwd_env_stateless_instance =
    std::is_empty_v<Query> && std::is_trivially_copyable_v<Query>;

template<class Query>
    requires __fwd_env_stateless_instance<std::remove_cvref_t<Query>> ||
             requires { std::remove_cvref_t<Query>{}; }
constexpr bool __classify_fwd_env_instance() {
    using query_t = std::remove_cvref_t<Query>;
    if constexpr (__fwd_env_stateless_instance<query_t>) {
        query_t instance = std::bit_cast<query_t>(std::array<std::byte, sizeof(query_t)>{});
        return std::forwarding_query(static_cast<Query&&>(instance));
    } else {
        query_t instance{};
        return std::forwarding_query(static_cast<Query&&>(instance));
    }
}

template<class Query>
concept __fwd_env_classifier_member = requires(Query&& q) {
    static_cast<Query&&>(q).query(std::forwarding_query);
};

// Without a classifier member the base-class rule of [exec.fwd.env]/2.2-2.3
// decides from the type alone. A classifier member that cannot be evaluated
// in a constant expression stays in the type-substitution failure path.
template<class Query>
concept __qualified_fwd_env_query =
    (!__fwd_env_classifier_member<Query> &&
     std::derived_from<std::remove_cvref_t<Query>, std::forwarding_query_t>) ||
    (__fwd_env_classifier_member<Query> &&
     requires { typename std::enable_if_t<__classify_fwd_env_instance<Query>()>; });

// A forwarded environment is queried as a const lvalue, whatever the value
// category of the expression that produced it. Env is the type of that
// expression.
template<class Query, class Env, class... Args>
concept __raw_queryable = requires(
    const std::remove_reference_t<Env>& env, Query&& q, Args&&... args) {
    env.query(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
};

template<class Query, class Env, class... Args>
concept __nothrow_raw_queryable = requires(
    const std::remove_reference_t<Env>& env, Query&& q, Args&&... args) {
    { env.query(static_cast<Query&&>(q), static_cast<Args&&>(args)...) } noexcept;
};

} // namespace __forge_env_detail

struct get_scheduler_t {
    template<class Env>
        requires (__forge_env_detail::__composed_scheduler_query<Env>())
    constexpr decltype(auto) operator()(Env&& env) const noexcept;

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_scheduler_t get_scheduler{};

struct get_start_scheduler_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_start_scheduler_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept {
        static_assert(__forge_env_detail::__nothrow_query<get_start_scheduler_t, Env>,
                      "query must be noexcept");
        static_assert(__forge_env_detail::__scheduler<std::remove_cvref_t<decltype(
            __forge_env_detail::__query(*this, static_cast<Env&&>(env)))>>(),
            "query must return a scheduler");
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_start_scheduler_t get_start_scheduler{};

struct get_delegation_scheduler_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_delegation_scheduler_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept {
        static_assert(__forge_env_detail::__nothrow_query<get_delegation_scheduler_t, Env>,
                      "query must be noexcept");
        static_assert(__forge_env_detail::__scheduler<std::remove_cvref_t<decltype(
            __forge_env_detail::__query(*this, static_cast<Env&&>(env)))>>(),
            "query must return a scheduler");
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_delegation_scheduler_t get_delegation_scheduler{};

enum class forward_progress_guarantee {
    concurrent,
    parallel,
    weakly_parallel
};

struct get_forward_progress_guarantee_t {
    template<class Scheduler>
        requires (__forge_env_detail::__scheduler_shape<Scheduler>()) &&
                 __forge_env_detail::__queryable<
            get_forward_progress_guarantee_t, Scheduler>
    constexpr decltype(auto) operator()(Scheduler&& scheduler) const
        noexcept {
        static_assert(__forge_env_detail::__nothrow_query<get_forward_progress_guarantee_t, Scheduler>,
                      "query must be noexcept");
        static_assert(std::same_as<std::remove_cvref_t<decltype(__forge_env_detail::__query(
            *this, static_cast<Scheduler&&>(scheduler)))>, forward_progress_guarantee>);
        return __forge_env_detail::__query(
            *this, static_cast<Scheduler&&>(scheduler));
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_forward_progress_guarantee_t get_forward_progress_guarantee{};

namespace __forge_env_detail {

template<class A>
concept __simple_allocator = requires(A a, std::size_t n) {
    { *a.allocate(n) } -> std::same_as<typename A::value_type&>;
    { a.deallocate(a.allocate(n), n) };
} && std::copy_constructible<A> && std::equality_comparable<A>;

} // namespace __forge_env_detail
} // namespace std::execution

namespace std {
struct get_stop_token_t {
    template<class Env>
        requires execution::__forge_env_detail::__queryable<get_stop_token_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept {
        static_assert(execution::__forge_env_detail::__nothrow_query<get_stop_token_t, Env>,
                      "query must be noexcept");
        using result_t = std::remove_cvref_t<decltype(
            execution::__forge_env_detail::__query(*this, static_cast<Env&&>(env)))>;
        static_assert(std::stoppable_token<result_t>, "invalid query result");
        return execution::__forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    template<class Env>
        requires (!execution::__forge_env_detail::__queryable<get_stop_token_t, Env>)
    constexpr std::never_stop_token operator()(Env&&) const noexcept { return {}; }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_allocator_t {
    template<class Env>
        requires execution::__forge_env_detail::__queryable<get_allocator_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept {
        static_assert(execution::__forge_env_detail::__nothrow_query<get_allocator_t, Env>,
                      "query must be noexcept");
        using result_t = std::remove_cvref_t<decltype(
            execution::__forge_env_detail::__query(*this, static_cast<Env&&>(env)))>;
        static_assert(execution::__forge_env_detail::__simple_allocator<result_t>, "invalid query result");
        return execution::__forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_allocator_t get_allocator{};

struct get_scheduler_allocator_t {
    template<class Env>
        requires execution::__forge_env_detail::__queryable<get_scheduler_allocator_t, Env> ||
                 execution::__forge_env_detail::__queryable<get_allocator_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const noexcept;

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};
inline constexpr get_scheduler_allocator_t get_scheduler_allocator{};

template<class Env>
    requires execution::__forge_env_detail::__queryable<get_scheduler_allocator_t, Env> ||
             execution::__forge_env_detail::__queryable<get_allocator_t, Env>
constexpr decltype(auto) get_scheduler_allocator_t::operator()(Env&& env) const noexcept {
    namespace detail = execution::__forge_env_detail;
    // Select the member tag before enforcing either Mandate.
    constexpr const auto& query = []() -> const auto& {
        if constexpr (detail::__queryable<get_scheduler_allocator_t, Env>) {
            return std::get_scheduler_allocator;
        } else {
            return std::get_allocator;
        }
    }();
    const auto& const_env = env;
    static_assert(noexcept(const_env.query(query)), "query must be noexcept");
    using result_t = std::remove_cvref_t<decltype(const_env.query(query))>;
    static_assert(detail::__simple_allocator<result_t>, "invalid query result");
    return const_env.query(query);
}

template<class T>
using stop_token_of_t = remove_cvref_t<decltype(std::get_stop_token(std::declval<T>()))>;
} // namespace std

namespace std::execution {

namespace __forge_env_detail {
template<class Tag>
struct __tag_holder {
#if defined(_MSC_VER)
    [[msvc::no_unique_address]] Tag tag;
#else
    [[no_unique_address]] Tag tag;
#endif
    constexpr __tag_holder() = default;
    constexpr __tag_holder(Tag t) noexcept(std::is_nothrow_move_constructible_v<Tag>)
        : tag(std::move(t)) {}
    __tag_holder(const __tag_holder&) = default;
    __tag_holder(__tag_holder&&) = default;
    __tag_holder& operator=(const __tag_holder&) = delete;
    __tag_holder& operator=(__tag_holder&&) = delete;
};
} // namespace __forge_env_detail

template<class Tag, class Value>
struct prop {
#if defined(_MSC_VER)
    [[msvc::no_unique_address]] __forge_env_detail::__tag_holder<Tag> tag_;
    [[msvc::no_unique_address]] Value value_;
#else
    [[no_unique_address]] __forge_env_detail::__tag_holder<Tag> tag_;
    [[no_unique_address]] Value value_;
#endif

    template<class... Args>
    constexpr const Value& query(Tag, Args&&...) const noexcept {
        return value_;
    }
};

template<class Tag, class Value>
prop(Tag, Value) -> prop<Tag, std::unwrap_reference_t<Value>>;

template<__forge_queryable... Envs>
struct env {
    constexpr env(const env&) = default;
    constexpr env(env&&) = default;
    env& operator=(const env&) = delete;
    constexpr explicit(sizeof...(Envs) != 0) env(Envs... envs)
        noexcept((std::is_nothrow_move_constructible_v<Envs> && ...))
        : __envs(std::forward<Envs>(envs)...)
    {}

private:
    // [exec.env] uses a temporary tag for participation and the named tag for selection.
    template<class E, class Q, class... Args>
    static constexpr bool __has_query = requires(const E& e, Args&&... args) {
        e.query(Q(), static_cast<Args&&>(args)...);
    };

    template<class E, class Q, class... Args>
        requires requires(const E& e, Q& q, Args&&... args) {
            e.query(q, static_cast<Args&&>(args)...);
        }
    static constexpr decltype(auto) __child_query(const E& e, Q& q, Args&&... args)
        noexcept(noexcept(e.query(q, static_cast<Args&&>(args)...))) {
        return e.query(q, static_cast<Args&&>(args)...);
    }

    // Specify the stored E explicitly: deduction through const E& would add
    // const to borrowed referents instead of preserving reference collapsing.
    template<class E, class Q, class... Args>
    static constexpr bool __answers_query = requires(const E& e, Q& q, Args&&... args) {
        __child_query<E>(e, q, static_cast<Args&&>(args)...);
    };

    template<std::size_t I, class Q, class... Args>
    static consteval bool __member_nothrow() {
        if constexpr (I == sizeof...(Envs)) {
            return true;
        } else {
            using E = std::tuple_element_t<I, std::tuple<Envs...>>;
            if constexpr (__answers_query<E, Q, Args...>) {
                return noexcept(__child_query<E>(std::declval<const E&>(),
                    std::declval<Q&>(), std::declval<Args>()...));
            } else {
                return __member_nothrow<I + 1, Q, Args...>();
            }
        }
    }

    template<std::size_t I, class Q, class... Args>
    constexpr decltype(auto) __member_query(Q& q, Args&&... args) const {
        static_assert(I < sizeof...(Envs));
        using E = std::tuple_element_t<I, std::tuple<Envs...>>;
        if constexpr (__answers_query<E, Q, Args...>) {
            return __child_query<E>(std::get<I>(__envs), q, static_cast<Args&&>(args)...);
        } else {
            return __member_query<I + 1>(q, static_cast<Args&&>(args)...);
        }
    }

public:
    template<class QueryTag, class... Args>
        requires (__has_query<Envs, QueryTag, Args...> || ...)
    constexpr decltype(auto) query(QueryTag q, Args&&... args) const
        noexcept(__member_nothrow<0, QueryTag, Args...>()) {
        return __member_query<0>(q, static_cast<Args&&>(args)...);
    }

private:
    [[no_unique_address]] std::tuple<Envs...> __envs;
};

// Match the constructor's constraint so Clang prefers this unwrapping guide.
template<__forge_queryable... Envs>
env(Envs...) -> env<std::unwrap_reference_t<Envs>...>;

struct get_env_t {
    template<class T>
    constexpr decltype(auto) operator()(const T& obj) const noexcept {
        if constexpr (__forge_env_detail::__member_get_env<T>) {
            static_assert(noexcept(obj.get_env()), "get_env must be noexcept");
            static_assert(__forge_queryable<std::remove_cvref_t<decltype(obj.get_env())>>);
            return obj.get_env();
        } else {
            return __empty_env{};
        }
    }
};
inline constexpr get_env_t get_env{};

template<class T>
using env_of_t = decltype(std::execution::get_env(std::declval<const T&>()));

namespace __forge_env_detail {

template<class Env>
using __normalized_env_t = std::remove_const_t<std::remove_reference_t<Env>>;

// FWD-ENV(get_env(rcvr)) of [exec.snd.expos]/4 for a receiver environment.
// Env is the environment type without reference and const, so the type of a
// view does not say whether get_env returned a reference or a prvalue;
// volatile stays, because it changes which members answer. The view never
// owns: it points at an environment that the receiver, or the operation state
// that holds the receiver, keeps alive, and it is copied like a pointer.
// Every query is made on the environment as a const lvalue, with the supplied
// query's own qualification.
template<class Env>
struct __fwd_view {
    const Env* __env;

    static constexpr __fwd_view __borrow(const Env& env) noexcept {
        return __fwd_view{std::addressof(env)};
    }

    template<class Query, class... Args>
        requires __qualified_fwd_env_query<Query> && __raw_queryable<Query, Env, Args...>
    constexpr decltype(auto) query(Query&& q, Args&&... args) const
        noexcept(__nothrow_raw_queryable<Query, Env, Args...>) {
        return __env->query(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
    }
};

// The view type depends on the environment type without reference and const.
template<class Env>
using __fwd_env_view = __fwd_view<__normalized_env_t<Env>>;

// The environment of the receiver that an operation state holds. A reference
// result of get_env is kept as that reference. A prvalue result is
// materialized here once, by direct initialization from the prvalue, so no
// constructor of the environment type takes part. The operation state
// outlives every view that a child can obtain from the receivers it creates
// ([exec.get.env]/2, [exec.queryable.general]/6).
template<class R>
struct __receiver_env {
    env_of_t<R> __env;

    constexpr explicit __receiver_env(const R& rcvr)
        noexcept(noexcept(std::execution::get_env(rcvr)))
        : __env(std::execution::get_env(rcvr)) {}

    __receiver_env(const __receiver_env&) = delete;
    __receiver_env& operator=(const __receiver_env&) = delete;

    // A named reference is an lvalue, whatever get_env returned.
    constexpr auto __get() const noexcept -> const __normalized_env_t<env_of_t<R>>& {
        return __env;
    }

    constexpr auto __view() const noexcept -> __fwd_env_view<env_of_t<R>> {
        return __fwd_env_view<env_of_t<R>>::__borrow(__env);
    }
};

// The attributes of a sender keep the exact type of get_env(sndr), as at
// 8de815c: a reference stays a reference, so lvalue-reference attributes that
// cannot be copied leave the view copyable, and a prvalue is owned. They are queried
// as a const lvalue as well.
template<class Env>
struct __attrs_operand {
    Env __env;

    template<class Source>
    constexpr explicit __attrs_operand(const Source& src) noexcept
        : __env(std::execution::get_env(src)) {}

    template<class Query, class... Args>
        requires __raw_queryable<Query, Env, Args...>
    constexpr decltype(auto) __call(Query&& q, Args&&... args) const
        noexcept(__nothrow_raw_queryable<Query, Env, Args...>) {
        return static_cast<const std::remove_reference_t<Env>&>(__env).query(
            static_cast<Query&&>(q), static_cast<Args&&>(args)...);
    }
};

// FWD-ENV(get_env(sndr)) for the attributes of an adaptor sender.
template<class Env>
struct __fwd_attrs_view {
    __attrs_operand<Env> __operand;

    template<class Source>
    static constexpr __fwd_attrs_view __of(const Source& src) noexcept {
        return __fwd_attrs_view{__attrs_operand<Env>(src)};
    }

    template<class Query, class... Args>
        requires __qualified_fwd_env_query<Query> && __raw_queryable<Query, Env, Args...>
    constexpr decltype(auto) query(Query&& q, Args&&... args) const
        noexcept(__nothrow_raw_queryable<Query, Env, Args...>) {
        return __operand.__call(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
    }
};

} // namespace __forge_env_detail

namespace __forge_env_detail {

template<class Tag>
concept __completion_tag = std::same_as<Tag, set_value_t> ||
    std::same_as<Tag, set_error_t> || std::same_as<Tag, set_stopped_t>;

// Sender attributes can rule out a completion in a supplied environment.
// Such an answer must not be resurrected by TRY-QUERY's unary fallback.
template<class Attrs, class Tag, class... Envs>
struct __completion_query_allowed : std::true_type {};

template<class Env, class Tag, class... Envs>
struct __completion_query_allowed<__fwd_view<Env>, Tag, Envs...>
    : __completion_query_allowed<std::remove_cv_t<Env>, Tag, Envs...> {};

template<class Env, class Tag, class... Envs>
struct __completion_query_allowed<__fwd_attrs_view<Env>, Tag, Envs...>
    : __completion_query_allowed<std::remove_cvref_t<Env>, Tag, Envs...> {};

template<class Q, class S, class... Envs>
concept __try_queryable = __queryable<Q, S, Envs...> || __queryable<Q, S>;

template<class Q, class S, class... Envs>
    requires __try_queryable<Q, S, Envs...>
constexpr decltype(auto) __try_query(Q q, S&& s, Envs&&... envs) noexcept {
    if constexpr (__queryable<Q, S, Envs...>) {
        static_assert(__nothrow_query<Q, S, Envs...>, "query must be noexcept");
        return __query(q, static_cast<S&&>(s), static_cast<Envs&&>(envs)...);
    } else {
        static_assert(__nothrow_query<Q, S>, "query must be noexcept");
        return __query(q, static_cast<S&&>(s));
    }
}

} // namespace __forge_env_detail

template<class CPO>
struct get_completion_scheduler_t;
template<class CPO>
inline constexpr get_completion_scheduler_t<CPO> get_completion_scheduler{};

namespace __forge_env_detail {

// S is the exact query result, so a terminal borrowed answer stays borrowed.
template<class S, class... Envs>
constexpr decltype(auto) __resolve_scheduler(S sch, Envs&&... envs) noexcept {
    using Q = get_completion_scheduler_t<std::conditional_t<true, set_value_t, S>>;
    if constexpr (!__try_queryable<Q, S&, Envs...>) {
        return static_cast<S>(static_cast<S&&>(sch));
    } else {
        using next_t = decltype(__try_query(Q{}, sch, static_cast<Envs&&>(envs)...));
        if constexpr (std::same_as<std::remove_cvref_t<S>, std::remove_cvref_t<next_t>>) {
            if constexpr (std::is_lvalue_reference_v<S> && std::same_as<S, next_t>) {
                auto* current = std::addressof(sch);
                for (;;) {
                    auto& next = __try_query(Q{}, *current, static_cast<Envs&&>(envs)...);
                    static_assert(noexcept(*current == next));
                    if (*current == next) return static_cast<S>(*current);
                    current = std::addressof(next);
                }
            } else {
                auto current = static_cast<S&&>(sch);
                for (;;) {
                    auto next = __try_query(Q{}, current, static_cast<Envs&&>(envs)...);
                    static_assert(noexcept(current == next));
                    if (current == next) return current;
                    current = std::move(next);
                }
            }
        } else {
            return __resolve_scheduler<next_t>(
                __try_query(Q{}, sch, static_cast<Envs&&>(envs)...),
                static_cast<Envs&&>(envs)...);
        }
    }
}

} // namespace __forge_env_detail

template<class CPO>
struct get_completion_scheduler_t {
    template<class Attrs, class... Envs>
        requires __forge_env_detail::__completion_tag<CPO> &&
                 __forge_env_detail::__completion_query_allowed<
                     std::remove_cvref_t<Attrs>, CPO, Envs...>::value &&
                 (__forge_env_detail::__try_queryable<get_completion_scheduler_t, Attrs, Envs...> ||
                  (sizeof...(Envs) != 0 && __forge_env_detail::__scheduler<Attrs>() &&
                   requires(Attrs&& a) { auto(static_cast<Attrs&&>(a)); }))
    constexpr decltype(auto) operator()(Attrs&& attrs, Envs&&... envs) const noexcept {
        if constexpr (__forge_env_detail::__try_queryable<get_completion_scheduler_t, Attrs, Envs...>) {
            using result_t = decltype(__forge_env_detail::__try_query(
                *this, static_cast<Attrs&&>(attrs), static_cast<Envs&&>(envs)...));
            using resolved_t = decltype(__forge_env_detail::__resolve_scheduler<result_t>(
                __forge_env_detail::__try_query(*this, static_cast<Attrs&&>(attrs),
                    static_cast<Envs&&>(envs)...), static_cast<Envs&&>(envs)...));
            static_assert(__forge_env_detail::__scheduler<std::remove_cvref_t<resolved_t>>(),
                          "query must return a scheduler");
            return __forge_env_detail::__resolve_scheduler<result_t>(
                __forge_env_detail::__try_query(*this, static_cast<Attrs&&>(attrs),
                    static_cast<Envs&&>(envs)...), static_cast<Envs&&>(envs)...);
        } else {
            static_assert(__forge_env_detail::__scheduler<std::remove_cvref_t<Attrs>>(),
                          "query must return a scheduler");
            return auto(static_cast<Attrs&&>(attrs));
        }
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};

namespace __forge_env_detail {

template<class Env>
struct __hide_scheduler {
    const Env* __env;

    // The supplied query keeps its qualification.
    template<class Query, class... Args>
        requires (!std::same_as<std::remove_cvref_t<Query>, get_scheduler_t> &&
                  !std::same_as<std::remove_cvref_t<Query>, get_domain_t> &&
                  __raw_queryable<Query, Env, Args...>)
    constexpr decltype(auto) query(Query&& q, Args&&... args) const
        noexcept(__nothrow_raw_queryable<Query, Env, Args...>) {
        return __env->query(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
    }
};

template<class Env>
consteval bool __composed_scheduler_query() {
    if constexpr (__queryable<get_scheduler_t, Env>) {
        return requires(Env&& env) {
            get_completion_scheduler<set_value_t>(
                __query(std::execution::get_scheduler, static_cast<Env&&>(env)),
                __hide_scheduler<std::remove_reference_t<Env>>{std::addressof(env)});
        };
    } else {
        return false;
    }
}

} // namespace __forge_env_detail

template<class Env>
    requires (__forge_env_detail::__composed_scheduler_query<Env>())
constexpr decltype(auto) get_scheduler_t::operator()(Env&& env) const noexcept {
    static_assert(__forge_env_detail::__nothrow_query<get_scheduler_t, Env>,
                  "query must be noexcept");
    using result_t = decltype(get_completion_scheduler<set_value_t>(
        __forge_env_detail::__query(*this, static_cast<Env&&>(env)),
        __forge_env_detail::__hide_scheduler<std::remove_reference_t<Env>>{std::addressof(env)}));
    static_assert(__forge_env_detail::__scheduler<std::remove_cvref_t<result_t>>(),
                  "query must return a scheduler");
    return get_completion_scheduler<set_value_t>(
        __forge_env_detail::__query(*this, static_cast<Env&&>(env)),
        __forge_env_detail::__hide_scheduler<std::remove_reference_t<Env>>{std::addressof(env)});
}

struct get_await_completion_adaptor_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_await_completion_adaptor_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept {
        static_assert(__forge_env_detail::__nothrow_query<get_await_completion_adaptor_t, Env>,
                      "query must be noexcept");
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};

inline constexpr get_await_completion_adaptor_t get_await_completion_adaptor{};

} // namespace std::execution

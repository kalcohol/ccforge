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

#include <concepts>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std {

struct forwarding_query_t {
    template<class Query>
    constexpr bool operator()(Query query) const noexcept {
        if constexpr (requires { query.query(*this); }) {
            static_assert(noexcept(query.query(*this)), "forwarding query must be noexcept");
            static_assert(std::same_as<decltype(query.query(*this)), bool>,
                          "forwarding query must return bool");
            return query.query(*this);
        } else if constexpr (std::execution::__forge_detail::tag_invocable<
                                 forwarding_query_t, Query>) {
            static_assert(std::execution::__forge_detail::nothrow_tag_invocable<
                              forwarding_query_t, Query>);
            static_assert(std::same_as<std::execution::__forge_detail::tag_invoke_result_t<
                              forwarding_query_t, Query>, bool>);
            return std::execution::__forge_detail::tag_invoke_fn(*this, query);
        } else if constexpr (std::derived_from<std::remove_cvref_t<Query>, forwarding_query_t>) {
            return true;
        } else {
            return false;
        }
    }
};

inline constexpr forwarding_query_t forwarding_query{};

} // namespace std

namespace std::execution {

template<class T>
concept __forge_queryable = std::destructible<T>;

template<class T>
concept queryable = __forge_queryable<T>;

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
// Deprecate the compatibility spelling without deprecating the underlying type.
using empty_env [[deprecated("use std::execution::env<> or forge::empty_env")]] = __empty_env;

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

// Test-only: define consistently in every translation unit of a test executable.
// Keep default constraint expressions unchanged: extra true atoms affect subsumption.
#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
template<class Query, class Env, class... Args>
concept __queryable = __member_query<Query, Env, Args...>;

template<class Query, class Env, class... Args>
inline constexpr bool __nothrow_query =
    __nothrow_member_query<Query, Env, Args...>;
#else
template<class Query, class Env, class... Args>
concept __queryable =
    __member_query<Query, Env, Args...> ||
    __forge_detail::tag_invocable<Query, Env, Args...>;

template<class Query, class Env, class... Args>
inline constexpr bool __nothrow_query =
    (__member_query<Query, Env, Args...> &&
     __nothrow_member_query<Query, Env, Args...>) ||
    (!__member_query<Query, Env, Args...> &&
     __forge_detail::nothrow_tag_invocable<Query, Env, Args...>);
#endif

template<class Query, class Env, class... Args>
    requires __queryable<Query, Env, Args...>
constexpr decltype(auto) __query(Query query, Env&& env, Args&&... args)
    noexcept(__nothrow_query<Query, Env, Args...>) {
    if constexpr (__member_query<Query, Env, Args...>) {
        return static_cast<const std::remove_reference_t<Env>&>(env)
            .query(query, static_cast<Args&&>(args)...);
    } else {
        return __forge_detail::tag_invoke_fn(
            std::move(query),
            static_cast<Env&&>(env),
            static_cast<Args&&>(args)...);
    }
}

template<class Query>
concept __forwarding_query = requires {
    // Keep non-constant classification in the type-substitution failure path.
    typename std::enable_if_t<
        std::forwarding_query(std::remove_cvref_t<Query>{})>;
};

// FWD-ENV classifies the query object; derived tags need no construction.
template<class Query>
concept __fwd_env_query =
    (std::derived_from<std::remove_cvref_t<Query>, std::forwarding_query_t> &&
     !requires(std::remove_cvref_t<Query>& q) { q.query(std::forwarding_query); } &&
     !__forge_detail::tag_invocable<std::forwarding_query_t, std::remove_cvref_t<Query>>) ||
    __forwarding_query<Query>;

// Env is the exact get_env result: own values, but retain borrowed references.
template<class Env>
struct __forwarding_attrs {
    using __env_t = std::remove_reference_t<Env>;
    // Keep prvalue ownership portable when attributes cannot be moved.
    Env __env;

    template<class Query, class... Args>
        requires __fwd_env_query<Query> &&
                 __queryable<Query, const __env_t&, Args...>
    constexpr decltype(auto) query(Query q, Args&&... args) const
        noexcept(__nothrow_query<Query, const __env_t&, Args...>) {
        return __query(std::move(q), static_cast<const __env_t&>(__env),
                       static_cast<Args&&>(args)...);
    }

    template<class Query, class... Args>
        requires __fwd_env_query<Query> &&
                 __queryable<Query, const __env_t&, Args...>
    friend constexpr decltype(auto) tag_invoke(
        Query q, const __forwarding_attrs& self, Args&&... args)
        noexcept(__nothrow_query<Query, const __env_t&, Args...>) {
        return self.query(std::move(q), static_cast<Args&&>(args)...);
    }
};

} // namespace __forge_env_detail

struct get_scheduler_t {
    template<class Env>
        requires (__forge_env_detail::__composed_scheduler_query<Env>())
    constexpr decltype(auto) operator()(Env&& env) const noexcept;

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_scheduler_t) noexcept {
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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_start_scheduler_t) noexcept {
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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_delegation_scheduler_t) noexcept {
        return true;
    }
};
inline constexpr get_delegation_scheduler_t get_delegation_scheduler{};

enum class forward_progress_guarantee {
    concurrent,
    parallel,
    weakly_parallel
};

namespace __forge_env_detail {

template<class Scheduler>
struct __progress_fallback {
    [[deprecated("provide scheduler.query(get_forward_progress_guarantee) const noexcept")]]
    static constexpr forward_progress_guarantee __get() noexcept {
        return forward_progress_guarantee::weakly_parallel;
    }
};

} // namespace __forge_env_detail

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

    template<class Scheduler>
        requires (__forge_env_detail::__scheduler_shape<Scheduler>()) &&
                 (!__forge_env_detail::__queryable<
                      get_forward_progress_guarantee_t, Scheduler> &&
                  requires { typename std::remove_cvref_t<Scheduler>::scheduler_concept; })
    constexpr forward_progress_guarantee operator()(Scheduler&&) const noexcept {
        return __forge_env_detail::__progress_fallback<Scheduler>::__get();
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_forward_progress_guarantee_t) noexcept {
        return true;
    }
};
inline constexpr get_forward_progress_guarantee_t get_forward_progress_guarantee{};

namespace __forge_env_detail {

template<class A>
concept __simple_allocator = requires(A a, std::size_t n) {
    typename A::value_type;
    requires std::copy_constructible<A>;
    requires std::equality_comparable<A>;
    { a.allocate(n) } -> std::same_as<typename A::value_type*>;
    { a.deallocate(a.allocate(n), n) } -> std::same_as<void>;
};

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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_stop_token_t) noexcept {
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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_allocator_t) noexcept {
        return true;
    }
};
inline constexpr get_allocator_t get_allocator{};

template<class T>
using stop_token_of_t = remove_cvref_t<decltype(std::get_stop_token(std::declval<T>()))>;
} // namespace std

namespace std::execution {
using std::forwarding_query_t;
using std::forwarding_query;
using std::get_stop_token_t;
using std::get_stop_token;
using std::get_allocator_t;
using std::get_allocator;
using std::stop_token_of_t;

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

template<class Tag, class Value>
[[deprecated("use std::execution::prop")]]
[[nodiscard]] auto make_prop(Tag tag, Value&& val) {
    return prop{tag, std::forward<Value>(val)};
}

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
    template<class Query>
    static constexpr bool __query_available =
        (__forge_env_detail::__queryable<Query, const Envs&> || ...);

    template<std::size_t I, class Query>
    static constexpr decltype(auto) __query(Query query, const env& self) {
        static_assert(I < sizeof...(Envs));
        using current_env_t = std::tuple_element_t<I, std::tuple<Envs...>>;
        if constexpr (__forge_env_detail::__queryable<Query, const current_env_t&>) {
            return __forge_env_detail::__query(
                std::move(query), std::get<I>(self.__envs));
        } else {
            return __query<I + 1>(std::move(query), self);
        }
    }

    template<std::size_t I, class Query>
    static consteval bool __query_nothrow() {
        if constexpr (I == sizeof...(Envs)) {
            return true;
        } else {
            using current_env_t = std::tuple_element_t<I, std::tuple<Envs...>>;
            if constexpr (__forge_env_detail::__queryable<Query, const current_env_t&>) {
                return __forge_env_detail::__nothrow_query<Query, const current_env_t&>;
            } else {
                return __query_nothrow<I + 1, Query>();
            }
        }
    }

    // [exec.env] uses a temporary tag for participation, but a named lvalue
    // for selection and invocation. Default mode retains compatibility dispatch.
    template<class E, class Q, class... Args>
    static constexpr bool __has_query = requires(const E& e, Args&&... args) {
        e.query(Q(), static_cast<Args&&>(args)...);
    };

#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
    template<class E, class Q, class... Args>
        requires requires(const E& e, Q& q, Args&&... args) {
            e.query(q, static_cast<Args&&>(args)...);
        }
    static constexpr decltype(auto) __child_query(const E& e, Q& q, Args&&... args)
        noexcept(noexcept(e.query(q, static_cast<Args&&>(args)...))) {
        return e.query(q, static_cast<Args&&>(args)...);
    }
#else
    template<class E, class Q, class... Args>
        requires __forge_env_detail::__member_query<Q, const E&, Args...> &&
                 requires(const E& e, Q& q, Args&&... args) {
                     e.query(q, static_cast<Args&&>(args)...);
                 }
    static constexpr decltype(auto) __child_query(const E& e, Q& q, Args&&... args)
        noexcept(noexcept(e.query(q, static_cast<Args&&>(args)...))) {
        return e.query(q, static_cast<Args&&>(args)...);
    }

    template<class E, class Q, class... Args>
        requires (!__forge_env_detail::__member_query<Q, const E&, Args...>) &&
                 __forge_detail::tag_invocable<Q, const E&, Args...>
    static constexpr decltype(auto) __child_query(const E& e, Q& q, Args&&... args)
        noexcept(noexcept(__forge_detail::tag_invoke_fn(
            std::move(q), e, static_cast<Args&&>(args)...))) {
        return __forge_detail::tag_invoke_fn(
            std::move(q), e, static_cast<Args&&>(args)...);
    }
#endif

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
    template<class Query>
        requires __query_available<Query>
    friend constexpr decltype(auto) tag_invoke(Query query, const env& self)
        noexcept(__query_nothrow<0, Query>()) {
        return __query<0>(std::move(query), self);
    }

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
        } else if constexpr (__forge_detail::tag_invocable<get_env_t, const T&>) {
            static_assert(__forge_detail::nothrow_tag_invocable<get_env_t, const T&>);
            static_assert(__forge_queryable<std::remove_cvref_t<
                __forge_detail::tag_invoke_result_t<get_env_t, const T&>>>);
            return __forge_detail::tag_invoke_fn(*this, obj);
        } else {
            return __empty_env{};
        }
    }
};
inline constexpr get_env_t get_env{};

template<class T>
using env_of_t = decltype(std::execution::get_env(std::declval<const T&>()));

template<class... Envs>
[[nodiscard]] auto make_env(Envs&&... envs) {
    return env{std::forward<Envs>(envs)...};
}

namespace __forge_env_detail {

template<class Tag>
concept __completion_tag = std::same_as<Tag, set_value_t> ||
    std::same_as<Tag, set_error_t> || std::same_as<Tag, set_stopped_t>;

// Sender attributes can rule out a completion in a supplied environment.
// Such an answer must not be resurrected by TRY-QUERY's unary fallback.
template<class Attrs, class Tag, class... Envs>
struct __completion_query_allowed : std::true_type {};

template<class Env, class Tag, class... Envs>
struct __completion_query_allowed<__forwarding_attrs<Env>, Tag, Envs...>
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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_completion_scheduler_t) noexcept {
        return true;
    }
};

namespace __forge_env_detail {

template<class Env>
struct __hide_scheduler {
    const Env* __env;

    template<class Query, class... Args>
        requires (!std::same_as<Query, get_scheduler_t> &&
                  !std::same_as<Query, get_domain_t> &&
                  __queryable<Query, const Env&, Args...>)
    constexpr decltype(auto) query(Query q, Args&&... args) const
        noexcept(__nothrow_query<Query, const Env&, Args...>) {
        return __query(q, *__env, static_cast<Args&&>(args)...);
    }
};

template<class Env>
consteval bool __composed_scheduler_query() {
    if constexpr (__queryable<get_scheduler_t, Env>) {
        return requires(Env&& env) {
            get_completion_scheduler<set_value_t>(
                __query(get_scheduler_t{}, static_cast<Env&&>(env)),
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

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_await_completion_adaptor_t) noexcept {
        return true;
    }
};

inline constexpr get_await_completion_adaptor_t get_await_completion_adaptor{};

} // namespace std::execution

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
#include <tuple>
#include <type_traits>
#include <utility>

namespace std {

struct forwarding_query_t {
    template<class Query>
    constexpr bool operator()(Query query) const noexcept {
        if constexpr (requires {
            { query.query(*this) } noexcept -> std::same_as<bool>;
        }) {
            return query.query(*this);
        } else if constexpr (std::execution::__forge_detail::tag_invocable<
                                 forwarding_query_t, Query>) {
            static_assert(std::execution::__forge_detail::nothrow_tag_invocable<
                              forwarding_query_t, Query>);
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

struct empty_env {};

namespace __forge_env_detail {

template<class T>
concept __member_get_env = requires(const T& obj) {
    obj.get_env();
};

template<class T>
concept __nothrow_member_get_env = requires(const T& obj) {
    { obj.get_env() } noexcept;
};

template<class Query, class Env>
concept __member_query = requires(
    const std::remove_reference_t<Env>& env,
    const Query& query) {
    env.query(query);
};

template<class Query, class Env>
concept __nothrow_member_query = requires(
    const std::remove_reference_t<Env>& env,
    const Query& query) {
    { env.query(query) } noexcept;
};

template<class Query, class Env>
concept __queryable =
    __member_query<Query, Env> ||
    __forge_detail::tag_invocable<Query, Env>;

template<class Query, class Env>
inline constexpr bool __nothrow_query =
    (__member_query<Query, Env> && __nothrow_member_query<Query, Env>) ||
    (!__member_query<Query, Env> &&
     __forge_detail::nothrow_tag_invocable<Query, Env>);

template<class Query, class Env>
    requires __queryable<Query, Env>
constexpr decltype(auto) __query(Query query, Env&& env)
    noexcept(__nothrow_query<Query, Env>) {
    if constexpr (__member_query<Query, Env>) {
        return static_cast<const std::remove_reference_t<Env>&>(env)
            .query(query);
    } else {
        return __forge_detail::tag_invoke_fn(
            std::move(query), static_cast<Env&&>(env));
    }
}

} // namespace __forge_env_detail

struct get_env_t {
    template<class T>
    auto operator()(const T& obj) const
        noexcept(__forge_env_detail::__nothrow_member_get_env<T> ||
                 (!__forge_env_detail::__member_get_env<T> &&
                  __forge_detail::nothrow_tag_invocable<get_env_t, const T&>) ||
                 (!__forge_env_detail::__member_get_env<T> &&
                  !__forge_detail::tag_invocable<get_env_t, const T&>)) {
        if constexpr (__forge_env_detail::__member_get_env<T>) {
            return obj.get_env();
        } else if constexpr (__forge_detail::tag_invocable<get_env_t, const T&>) {
            return __forge_detail::tag_invoke_fn(*this, obj);
        } else {
            return empty_env{};
        }
    }
};
inline constexpr get_env_t get_env{};

template<class T>
using env_of_t = decltype(std::execution::get_env(std::declval<const T&>()));

struct get_scheduler_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_scheduler_t, Env>
    decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<get_scheduler_t, Env>) {
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_scheduler_t) noexcept {
        return true;
    }
};
inline constexpr get_scheduler_t get_scheduler{};

struct get_start_scheduler_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_start_scheduler_t, Env>
    constexpr decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<get_start_scheduler_t, Env>) {
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
        noexcept(__forge_env_detail::__nothrow_query<
            get_delegation_scheduler_t, Env>) {
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

struct get_forward_progress_guarantee_t {
    template<class Scheduler>
        requires __forge_env_detail::__queryable<
            get_forward_progress_guarantee_t, Scheduler>
    constexpr decltype(auto) operator()(Scheduler&& scheduler) const
        noexcept(__forge_env_detail::__nothrow_query<
            get_forward_progress_guarantee_t, Scheduler>) {
        return __forge_env_detail::__query(
            *this, static_cast<Scheduler&&>(scheduler));
    }

    template<class Scheduler>
        requires (!__forge_env_detail::__queryable<
                      get_forward_progress_guarantee_t, Scheduler> &&
                  requires { typename std::remove_cvref_t<Scheduler>::scheduler_concept; })
    constexpr forward_progress_guarantee operator()(Scheduler&&) const noexcept {
        return forward_progress_guarantee::weakly_parallel;
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_forward_progress_guarantee_t) noexcept {
        return true;
    }
};
inline constexpr get_forward_progress_guarantee_t get_forward_progress_guarantee{};

struct get_stop_token_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_stop_token_t, Env>
    decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<get_stop_token_t, Env>) {
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    template<class Env>
        requires (!__forge_env_detail::__queryable<get_stop_token_t, Env>)
    std::never_stop_token operator()(Env&&) const noexcept { return {}; }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_stop_token_t) noexcept {
        return true;
    }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_allocator_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_allocator_t, Env>
    decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<get_allocator_t, Env>) {
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_allocator_t) noexcept {
        return true;
    }
};
inline constexpr get_allocator_t get_allocator{};


template<class Tag, class Value>
struct prop {
    [[no_unique_address]] Tag tag_;
    [[no_unique_address]] Value value_;

    template<class... Args>
    constexpr const Value& query(Tag, Args&&...) const noexcept {
        return value_;
    }
};

template<class Tag, class Value>
prop(Tag, Value) -> prop<Tag, std::unwrap_reference_t<Value>>;

template<class Tag, class Value>
[[nodiscard]] auto make_prop(Tag tag, Value&& val) {
    return prop{tag, std::forward<Value>(val)};
}

template<class... Envs>
struct env {
    constexpr explicit env(Envs... envs)
        noexcept((std::is_nothrow_move_constructible_v<Envs> && ...))
        : __envs(std::move(envs)...)
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

    template<class Query>
        requires __query_available<Query>
    friend constexpr decltype(auto) tag_invoke(Query query, const env& self)
        noexcept(__query_nothrow<0, Query>()) {
        return __query<0>(std::move(query), self);
    }

    [[no_unique_address]] std::tuple<Envs...> __envs;
};

template<class... Envs>
env(Envs...) -> env<Envs...>;

template<class... Envs>
[[nodiscard]] auto make_env(Envs&&... envs) {
    return env<std::decay_t<Envs>...>{std::forward<Envs>(envs)...};
}

// get_completion_scheduler CPO — [exec.getcomplsched]
template<class CPO>
struct get_completion_scheduler_t {
    template<class Env>
        requires __forge_env_detail::__queryable<
            get_completion_scheduler_t<CPO>, Env>
    decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<
            get_completion_scheduler_t<CPO>, Env>) {
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_completion_scheduler_t) noexcept {
        return true;
    }
};
template<class CPO>
inline constexpr get_completion_scheduler_t<CPO> get_completion_scheduler{};

struct get_await_completion_adaptor_t {
    template<class Env>
        requires __forge_env_detail::__queryable<get_await_completion_adaptor_t, Env>
    decltype(auto) operator()(Env&& env) const
        noexcept(__forge_env_detail::__nothrow_query<
            get_await_completion_adaptor_t, Env>) {
        return __forge_env_detail::__query(*this, static_cast<Env&&>(env));
    }

    friend constexpr bool tag_invoke(std::forwarding_query_t, get_await_completion_adaptor_t) noexcept {
        return true;
    }
};

inline constexpr get_await_completion_adaptor_t get_await_completion_adaptor{};

} // namespace std::execution

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

#include "concepts.hpp"
#include "counting_scope.hpp"
#include "env.hpp"
#include "write_env.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_spawn {

template<class Sig>
struct __spawn_sig_ok : std::false_type {};

template<>
struct __spawn_sig_ok<set_value_t()> : std::true_type {};

template<>
struct __spawn_sig_ok<set_stopped_t()> : std::true_type {};

template<class CS>
struct __spawnable_completions : std::false_type {};

template<class... Sigs>
struct __spawnable_completions<completion_signatures<Sigs...>>
    : std::bool_constant<(__spawn_sig_ok<Sigs>::value && ...)> {};

template<class CS>
inline constexpr bool __spawnable_completions_v =
    __spawnable_completions<CS>::value;

template<class Env>
concept __has_allocator_query = requires(const Env& env) {
    std::execution::get_allocator(env);
};

template<class Alloc, class S, class Token>
struct __state {
    using state_t = __state;
    using association_t = decltype(std::declval<Token&>().try_associate());

    struct __recv {
        using receiver_concept = receiver_t;

        state_t* __owner;

        void set_value() && noexcept {
            __owner->__complete();
        }

        void set_stopped() && noexcept {
            __owner->__complete();
        }

        auto get_env() const noexcept -> empty_env {
            return {};
        }
    };

    using op_t = connect_result_t<S, __recv>;

    Alloc __alloc;
    std::atomic<unsigned> __refs{1};
    association_t __association;
    op_t __op;

    __state(Alloc alloc, S sndr, Token token)
        : __alloc(std::move(alloc))
        , __association(token.try_associate())
        , __op(std::execution::connect(std::move(sndr), __recv{this}))
    {}

    void __add_ref() noexcept {
        __refs.fetch_add(1, std::memory_order_relaxed);
    }

    void __release() noexcept {
        if (__refs.fetch_sub(1, std::memory_order_acq_rel) != 1) {
            return;
        }

        using traits_t = std::allocator_traits<Alloc>;
        using rebound_alloc_t = typename traits_t::template rebind_alloc<state_t>;
        using rebound_traits_t = std::allocator_traits<rebound_alloc_t>;
        rebound_alloc_t alloc{__alloc};
        rebound_traits_t::destroy(alloc, this);
        rebound_traits_t::deallocate(alloc, this, 1);
    }

    void __complete() noexcept {
        __association = {};
        __release();
    }

    void __run() noexcept {
        __add_ref();
        if (__association) {
            std::execution::start(__op);
        } else {
            __complete();
        }
        __release();
    }
};

template<class S, class Token, class Alloc>
void __launch(S sndr, Token token, Alloc alloc) {
    using source_t = std::decay_t<S>;
    using token_t = std::decay_t<Token>;
    using raw_alloc_t = std::decay_t<Alloc>;
    using state_t = __state<raw_alloc_t, source_t, token_t>;
    using traits_t = std::allocator_traits<raw_alloc_t>;
    using state_alloc_t = typename traits_t::template rebind_alloc<state_t>;
    using state_traits_t = std::allocator_traits<state_alloc_t>;

    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<source_t>(), std::declval<empty_env>()));
    static_assert(__spawnable_completions_v<cs_t>,
        "std::execution::spawn requires a sender that completes with set_value() or set_stopped()");

    state_alloc_t state_alloc{alloc};
    auto* state = state_traits_t::allocate(state_alloc, 1);
    try {
        state_traits_t::construct(
            state_alloc, state, raw_alloc_t{state_alloc}, std::move(sndr), std::move(token));
    } catch (...) {
        state_traits_t::deallocate(state_alloc, state, 1);
        throw;
    }
    state->__run();
}

template<class S, class Token, class Env, class Alloc>
void __launch_with_env(S sndr, Token token, Env env, Alloc alloc) {
    auto started = std::execution::write_env(std::move(sndr), std::move(env));
    __launch(std::move(started), std::move(token), std::move(alloc));
}

template<class Wrapped, class Token, class Env>
void __spawn_wrapped(Wrapped wrapped, Token token, Env env) {
    if constexpr (__has_allocator_query<Env>) {
        auto alloc = std::execution::get_allocator(env);
        __launch_with_env(std::move(wrapped), std::move(token), std::move(env), std::move(alloc));
    } else if constexpr (requires(const Wrapped& w) {
        std::execution::get_allocator(std::execution::get_env(w));
    }) {
        auto source_env = std::execution::get_env(wrapped);
        auto alloc = std::execution::get_allocator(source_env);
        auto joined_env = std::execution::make_env(
            std::execution::make_prop(std::execution::get_allocator_t{}, alloc),
            std::move(env));
        __launch_with_env(
            std::move(wrapped), std::move(token), std::move(joined_env), std::move(alloc));
    } else {
        __launch_with_env(
            std::move(wrapped),
            std::move(token),
            std::move(env),
            std::allocator<std::byte>{});
    }
}

struct __spawn_t {
    template<sender S, scope_token Token, queryable Env>
    void operator()(S&& sndr, Token token, Env env) const {
        auto wrapped = token.wrap(static_cast<S&&>(sndr));
        __spawn_wrapped(std::move(wrapped), std::move(token), std::move(env));
    }

    template<sender S, scope_token Token>
    void operator()(S&& sndr, Token token) const {
        (*this)(static_cast<S&&>(sndr), std::move(token), empty_env{});
    }
};

} // namespace __forge_spawn

inline constexpr __forge_spawn::__spawn_t spawn{};

} // namespace std::execution

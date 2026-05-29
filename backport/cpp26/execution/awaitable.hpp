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
#include "env.hpp"
#include "sync_wait.hpp"

// Coroutine bridge — only compiled when coroutines are available
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_awaitable {

struct __stopped_awaitable_exception {};

template<class Promise>
concept __has_unhandled_stopped = requires(Promise& p) {
    { p.unhandled_stopped() } noexcept -> std::convertible_to<std::coroutine_handle<>>;
};

template<class Promise>
concept __has_promise_env = requires(const Promise& p) {
    std::execution::get_env(p);
};

template<class Promise>
auto __get_promise_env(const Promise& p)
    noexcept(noexcept(std::execution::get_env(p)))
    requires __has_promise_env<Promise> {
    return std::execution::get_env(p);
}

template<class Promise>
    requires (!__has_promise_env<Promise>)
auto __get_promise_env(const Promise&) noexcept -> empty_env {
    return {};
}

template<class CS>
struct __value_tuple_list;

template<class... Sigs>
struct __value_tuple_list<completion_signatures<Sigs...>> {
private:
    template<class List, class Sig>
    struct __push_value {
        using type = List;
    };

    template<class... Tuples, class... Vs>
    struct __push_value<__forge_meta::type_list<Tuples...>, set_value_t(Vs...)> {
        using tuple_t = std::tuple<std::decay_t<Vs>...>;
        using type = __forge_meta::list_push_unique_t<
            __forge_meta::type_list<Tuples...>, tuple_t>;
    };

    template<class List, class... Rest>
    struct __collect;

    template<class List>
    struct __collect<List> {
        using type = List;
    };

    template<class List, class Sig, class... Rest>
    struct __collect<List, Sig, Rest...> {
        using next = typename __push_value<List, Sig>::type;
        using type = typename __collect<next, Rest...>::type;
    };

public:
    using type = typename __collect<__forge_meta::type_list<>, Sigs...>::type;
};

template<class CS>
using __value_tuple_list_t = typename __value_tuple_list<CS>::type;

template<class List>
struct __await_result_value;

template<>
struct __await_result_value<__forge_meta::type_list<>> {
    using type = std::tuple<>;
};

template<class Tuple>
struct __await_result_value<__forge_meta::type_list<Tuple>> {
    using type = Tuple;
};

template<class... Tuples>
struct __await_result_value<__forge_meta::type_list<Tuples...>> {
    using type = std::variant<Tuples...>;
};

template<class CS>
using __await_result_value_t =
    typename __await_result_value<__value_tuple_list_t<CS>>::type;

template<class S, class Promise>
struct __awaitable {
    S __sndr;
    Promise* __promise;

    using env_t = decltype(__get_promise_env(std::declval<const Promise&>()));
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<env_t>()));
    using value_t = __await_result_value_t<cs_t>;
    using result_t = std::optional<value_t>;

    struct __recv {
        using receiver_concept = receiver_t;
        __awaitable* __self;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            try {
                using tuple_t = std::tuple<std::decay_t<Vs>...>;
                __self->__result.emplace(tuple_t{static_cast<Vs&&>(vs)...});
            } catch (...) {
                __self->__exc = std::current_exception();
            }
            __self->__coro.resume();
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                __self->__exc = static_cast<E&&>(e);
            else
                __self->__exc = std::make_exception_ptr(static_cast<E&&>(e));
            __self->__coro.resume();
        }
        void set_stopped() && noexcept {
            if constexpr (__has_unhandled_stopped<Promise>) {
                static_cast<std::coroutine_handle<>>(
                    __self->__promise->unhandled_stopped()).resume();
            } else {
                __self->__stopped = true;
                __self->__coro.resume();
            }
        }
        auto get_env() const noexcept -> env_t {
            return __get_promise_env(*__self->__promise);
        }
    };

    using op_t = connect_result_t<S, __recv>;

    result_t __result;
    std::exception_ptr __exc;
    bool __stopped = false;
    std::coroutine_handle<> __coro;

    alignas(op_t) unsigned char __op_buf[sizeof(op_t)];
    bool __op_constructed = false;
    void (*__op_dtor)(void*) noexcept = nullptr;

    ~__awaitable() noexcept {
        if (__op_constructed && __op_dtor) __op_dtor(__op_buf);
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<Promise> h) {
        __coro = h;
        ::new(__op_buf) op_t(std::execution::connect(std::move(__sndr), __recv{this}));
        __op_constructed = true;
        __op_dtor = [](void* p) noexcept { static_cast<op_t*>(p)->~op_t(); };
        std::execution::start(*static_cast<op_t*>(static_cast<void*>(__op_buf)));
    }

    auto await_resume() {
        if (__exc) std::rethrow_exception(__exc);
        if (__stopped) throw __stopped_awaitable_exception{};
        return std::move(*__result);
    }
};

} // namespace __forge_awaitable

// as_awaitable(sndr) — [exec.as.awaitable]
// Returns an awaitable that bridges the sender into a coroutine context.
template<sender S, class Promise>
[[nodiscard]] auto as_awaitable(S sndr, Promise& promise) {
    return __forge_awaitable::__awaitable<S, Promise>{
        std::move(sndr), &promise};
}

// with_awaitable_senders<Promise> — CRTP mixin — [exec.with.awaitable.senders]
// Makes a coroutine promise_type support co_await on senders.
template<class Promise>
struct with_awaitable_senders {
    template<sender S>
    auto await_transform(S&& sndr) {
        auto& prom = static_cast<Promise&>(*this);
        return as_awaitable(std::forward<S>(sndr), prom);
    }
};

} // namespace std::execution

#endif // __cpp_impl_coroutine

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
#include "detail/value_result.hpp"
#include "env.hpp"
#include "sync_wait.hpp"

// Coroutine bridge — only compiled when coroutines are available
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_awaitable {

struct __stopped_awaitable_exception {};
struct __unit {};

template<class T>
struct __is_coroutine_handle : std::false_type {};

template<class Promise>
struct __is_coroutine_handle<std::coroutine_handle<Promise>> : std::true_type {};

template<class T>
concept __await_suspend_result =
    std::same_as<T, void> ||
    std::same_as<T, bool> ||
    __is_coroutine_handle<std::remove_cvref_t<T>>::value;

template<class Awaiter, class Promise>
concept __awaiter_for = requires(
    std::remove_reference_t<Awaiter>& awaiter,
    std::coroutine_handle<Promise> continuation) {
    awaiter.await_ready() ? 1 : 0;
    requires __await_suspend_result<
        decltype(awaiter.await_suspend(continuation))>;
    awaiter.await_resume();
};

template<class T>
decltype(auto) __get_awaiter(T&& value) {
    if constexpr (requires {
                      static_cast<T&&>(value).operator co_await();
                  }) {
        return static_cast<T&&>(value).operator co_await();
    } else if constexpr (requires {
                             operator co_await(static_cast<T&&>(value));
                         }) {
        return operator co_await(static_cast<T&&>(value));
    } else {
        return static_cast<T&&>(value);
    }
}

template<class T, class Promise>
concept __ordinary_awaitable = requires(T&& value) {
    requires __awaiter_for<
        decltype(__get_awaiter(static_cast<T&&>(value))), Promise>;
};

template<class T, class Promise>
concept __has_as_awaitable_member = requires(T&& value, Promise& promise) {
    static_cast<T&&>(value).as_awaitable(promise);
};

template<class Tuple>
struct __value_from_tuple;

template<>
struct __value_from_tuple<std::tuple<>> {
    using type = void;
};

template<class T>
struct __value_from_tuple<std::tuple<T>> {
    using type = T;
};

template<class T, class U, class... Rest>
struct __value_from_tuple<std::tuple<T, U, Rest...>> {
    using type = std::tuple<T, U, Rest...>;
};

template<class List>
struct __single_sender_value {};

template<>
struct __single_sender_value<__forge_meta::type_list<>> {
    using type = void;
};

template<class Tuple>
struct __single_sender_value<__forge_meta::type_list<Tuple>>
    : __value_from_tuple<Tuple> {};

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

template<class S, class Promise>
struct __sender_value
    : __single_sender_value<__forge_meta::value_tuple_list_t<decltype(
          std::execution::get_completion_signatures(
              std::declval<S>(),
              std::declval<decltype(__get_promise_env(
                  std::declval<const Promise&>()))>()))>> {
    using env_t = decltype(__get_promise_env(std::declval<const Promise&>()));
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<env_t>()));
};

template<class S, class Promise>
concept __single_sender = requires {
    typename __sender_value<S, Promise>::type;
};

template<class S, class Promise>
auto __transform_for_await(S&& sndr, Promise& promise) {
    auto env = __get_promise_env(promise);
    return std::execution::transform_sender(
        static_cast<S&&>(sndr), env);
}

template<class S>
decltype(auto) __adapt_for_await_completion(S&& sndr) {
    if constexpr (requires {
                      std::execution::get_await_completion_adaptor(
                          std::execution::get_env(sndr))(
                              static_cast<S&&>(sndr));
                  }) {
        return std::execution::get_await_completion_adaptor(
            std::execution::get_env(sndr))(static_cast<S&&>(sndr));
    } else {
        return static_cast<S&&>(sndr);
    }
}

template<class S, class Promise>
auto __prepare_sender_for_await(S&& sndr, Promise& promise) {
    auto transformed = __transform_for_await(
        static_cast<S&&>(sndr), promise);
    return __adapt_for_await_completion(std::move(transformed));
}

template<class S, class Promise>
concept __transformed_sender_has_as_awaitable =
    sender_in<S, decltype(__get_promise_env(std::declval<const Promise&>()))> &&
    __single_sender<std::decay_t<S>, Promise> &&
    requires(S&& sndr, Promise& promise) {
        __prepare_sender_for_await(
            static_cast<S&&>(sndr), promise).as_awaitable(promise);
    };

template<class S, class Promise>
using __prepared_sender_t = std::decay_t<decltype(
    __prepare_sender_for_await(
        std::declval<S>(), std::declval<Promise&>()))>;

template<class S, class Promise>
concept __bridgeable_sender =
    sender_in<S, decltype(__get_promise_env(std::declval<const Promise&>()))> &&
    __single_sender<std::decay_t<S>, Promise> &&
    __single_sender<__prepared_sender_t<S, Promise>, Promise>;

template<class Value, class Promise>
consteval bool __as_awaitable_nothrow() {
    if constexpr (__has_as_awaitable_member<Value, Promise>) {
        return noexcept(std::declval<Value>().as_awaitable(
            std::declval<Promise&>()));
    } else if constexpr (__transformed_sender_has_as_awaitable<Value, Promise>) {
        return false;
    } else if constexpr (__ordinary_awaitable<Value, Promise>) {
        return true;
    } else if constexpr (__bridgeable_sender<Value, Promise>) {
        return false;
    } else {
        return true;
    }
}

template<class S, class Promise>
struct __awaitable {
    S __sndr;
    Promise* __promise;

    __awaitable(S sndr, Promise* promise)
        : __sndr(std::move(sndr)), __promise(promise) {}

    using env_t = typename __sender_value<S, Promise>::env_t;
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<env_t>()));
    using value_t = typename __sender_value<S, Promise>::type;
    using stored_value_t = std::conditional_t<
        std::is_void_v<value_t>, __unit, value_t>;
    using result_t = std::optional<stored_value_t>;

    struct __recv {
        using receiver_concept = receiver_t;
        __awaitable* __self;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            try {
                if constexpr (std::is_void_v<value_t>) {
                    static_assert(sizeof...(Vs) == 0);
                    __self->__result.emplace();
                } else {
                    __self->__result.emplace(static_cast<Vs&&>(vs)...);
                }
            } catch (...) {
                __self->__exc = std::current_exception();
            }
            __self->__complete(__self->__coro);
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                __self->__exc = static_cast<E&&>(e);
            else
                __self->__exc = std::make_exception_ptr(static_cast<E&&>(e));
            __self->__complete(__self->__coro);
        }
        void set_stopped() && noexcept {
            __self->__stopped = true;
            if constexpr (__has_unhandled_stopped<Promise>) {
                __self->__complete(static_cast<std::coroutine_handle<>>(
                    __self->__promise->unhandled_stopped()));
            } else {
                __self->__complete(__self->__coro);
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

    auto await_suspend(std::coroutine_handle<Promise> h)
        -> std::coroutine_handle<> {
        __coro = h;
        ::new(__op_buf) op_t(std::execution::connect(std::move(__sndr), __recv{this}));
        __op_constructed = true;
        __op_dtor = [](void* p) noexcept { static_cast<op_t*>(p)->~op_t(); };
        std::execution::start(*static_cast<op_t*>(static_cast<void*>(__op_buf)));
        if (__state.exchange(
                __completion_state::suspended,
                std::memory_order_acq_rel) == __completion_state::completed) {
            return __resume;
        }
        return std::noop_coroutine();
    }

    auto await_resume() -> value_t {
        if (__exc) std::rethrow_exception(__exc);
        if (__stopped) throw __stopped_awaitable_exception{};
        if constexpr (!std::is_void_v<value_t>) {
            return std::move(*__result);
        }
    }

private:
    enum class __completion_state : unsigned char {
        starting,
        suspended,
        completed
    };

    void __complete(std::coroutine_handle<> next) noexcept {
        __resume = next;
        if (__state.exchange(
                __completion_state::completed,
                std::memory_order_acq_rel) == __completion_state::suspended) {
            next.resume();
        }
    }

    std::coroutine_handle<> __resume{};
    std::atomic<__completion_state> __state{__completion_state::starting};
};

} // namespace __forge_awaitable

// as_awaitable(expr, promise) — [exec.as.awaitable]
struct as_awaitable_t {
    template<class Value, class Promise>
    [[nodiscard]] decltype(auto) operator()(Value&& value, Promise& promise) const
        noexcept(__forge_awaitable::__as_awaitable_nothrow<Value, Promise>()) {
        using namespace __forge_awaitable;

        if constexpr (__has_as_awaitable_member<Value, Promise>) {
            return static_cast<Value&&>(value).as_awaitable(promise);
        } else if constexpr (__transformed_sender_has_as_awaitable<Value, Promise>) {
            auto prepared = __prepare_sender_for_await(
                static_cast<Value&&>(value), promise);
            return std::move(prepared).as_awaitable(promise);
        } else if constexpr (__ordinary_awaitable<Value, Promise>) {
            return static_cast<Value&&>(value);
        } else if constexpr (__bridgeable_sender<Value, Promise>) {
            using sender_t = __prepared_sender_t<Value, Promise>;
            return __awaitable<sender_t, Promise>{
                __prepare_sender_for_await(
                    static_cast<Value&&>(value), promise),
                &promise};
        } else {
            return static_cast<Value&&>(value);
        }
    }
};

inline constexpr as_awaitable_t as_awaitable{};

// with_awaitable_senders<Promise> — CRTP mixin — [exec.with.awaitable.senders]
// Makes a coroutine promise_type support co_await on senders.
template<class Promise>
struct with_awaitable_senders {
    template<class OtherPromise>
        requires (!std::same_as<OtherPromise, void>)
    void set_continuation(std::coroutine_handle<OtherPromise> continuation) noexcept {
        __continuation = continuation;
        if constexpr (requires(OtherPromise& promise) {
                          { promise.unhandled_stopped() }
                              -> std::convertible_to<std::coroutine_handle<>>;
                      }) {
            __stopped_handler = [](void* address) noexcept
                -> std::coroutine_handle<> {
                return std::coroutine_handle<OtherPromise>::from_address(address)
                    .promise()
                    .unhandled_stopped();
            };
        } else {
            __stopped_handler = &__default_unhandled_stopped;
        }
    }

    std::coroutine_handle<> continuation() const noexcept {
        return __continuation;
    }

    std::coroutine_handle<> unhandled_stopped() noexcept {
        return __stopped_handler(__continuation.address());
    }

    template<class Value>
    decltype(auto) await_transform(Value&& value) {
        auto& prom = static_cast<Promise&>(*this);
        return as_awaitable(std::forward<Value>(value), prom);
    }

private:
    [[noreturn]] static std::coroutine_handle<>
    __default_unhandled_stopped(void*) noexcept {
        std::terminate();
    }

    std::coroutine_handle<> __continuation{};
    std::coroutine_handle<> (*__stopped_handler)(void*) noexcept =
        &__default_unhandled_stopped;
};

} // namespace std::execution

#endif // __cpp_impl_coroutine

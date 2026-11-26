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
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_awaitable {

struct __unit {};

// Only completion on the starting thread needs deferral. The stack-owned
// scope survives even if another completion agent destroys the awaitable.
struct __start_scope {
    explicit __start_scope(const void* key) noexcept
        : __key(key), __previous(__current) {
        __current = this;
    }

    ~__start_scope() { __current = __previous; }

    static bool __defer(const void* key, std::coroutine_handle<> next) noexcept {
        for (auto* scope = __current; scope; scope = scope->__previous) {
            if (scope->__key == key) {
                scope->__resume = next;
                return true;
            }
        }
        return false;
    }

    const void* __key;
    __start_scope* __previous;
    std::coroutine_handle<> __resume = std::noop_coroutine();
    inline static thread_local __start_scope* __current = nullptr;
};

// [exec.as.awaitable]/7.3 uses GET-AWAITER(expr) without an await_transform.
template<class T, class Promise>
concept __ordinary_awaitable = requires {
    requires __is_awaiter<
        decltype(__forge_awaitable::__get_awaiter(std::declval<T>())), Promise>;
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

template<class Promise>
concept __has_unhandled_stopped = requires(Promise& p) {
    static_cast<std::coroutine_handle<>>(p.unhandled_stopped());
};

template<class Promise>
concept __has_promise_env = requires(const Promise& p) {
    std::execution::get_env(p);
};

template<class Promise>
decltype(auto) __get_promise_env(const Promise& p)
    noexcept(noexcept(std::execution::get_env(p)))
    requires __has_promise_env<Promise> {
    return std::execution::get_env(p);
}

template<class Promise>
    requires (!__has_promise_env<Promise>)
auto __get_promise_env(const Promise&) noexcept -> __empty_env {
    return {};
}

template<class Promise>
using __promise_env_t = decltype(__get_promise_env(std::declval<const Promise&>()));

template<class S, class Promise>
using __sender_completions_t = completion_signatures_of_t<S, __promise_env_t<Promise>>;

template<class S, class Promise>
concept __single_sender = requires {
    typename __sender_completions_t<S, Promise>;
    // Count signatures before decaying the arguments of the selected signature.
    requires (__forge_meta::__value_count<__sender_completions_t<S, Promise>> <= 1);
};

template<class S, class Promise>
auto __transform_for_await(S&& sndr, Promise& promise)
    noexcept(noexcept(std::execution::transform_sender(
        static_cast<S&&>(sndr), __get_promise_env(promise))))
    -> decltype(std::execution::transform_sender(
        static_cast<S&&>(sndr), __get_promise_env(promise))) {
    return std::execution::transform_sender(
        static_cast<S&&>(sndr), __get_promise_env(promise));
}

template<class S>
concept __has_await_adaptor = requires(S&& sndr) {
    std::execution::get_await_completion_adaptor(std::execution::get_env(sndr))(
        static_cast<S&&>(sndr));
};

template<class S> requires __has_await_adaptor<S>
auto __adapt_for_await_completion(S&& sndr)
    noexcept(noexcept(std::execution::get_await_completion_adaptor(
        std::execution::get_env(sndr))(static_cast<S&&>(sndr))))
    -> decltype(std::execution::get_await_completion_adaptor(
        std::execution::get_env(sndr))(static_cast<S&&>(sndr))) {
    return std::execution::get_await_completion_adaptor(
        std::execution::get_env(sndr))(static_cast<S&&>(sndr));
}

template<class S> requires (!__has_await_adaptor<S>)
auto __adapt_for_await_completion(S&& sndr) noexcept -> S&& {
    return static_cast<S&&>(sndr);
}

template<class S, class Promise>
concept __transformed_sender_has_as_awaitable =
    __single_sender<S, Promise> &&
    requires(S&& sndr, Promise& promise) {
        __adapt_for_await_completion(__transform_for_await(
            static_cast<S&&>(sndr), promise)).as_awaitable(promise);
    };

template<class S, class Promise>
using __transformed_sender_t = decltype(__transform_for_await(
    std::declval<S>(), std::declval<Promise&>()));

template<class S>
using __adapted_sender_t = decltype(__adapt_for_await_completion(std::declval<S>()));

template<class S, class Promise>
using __prepared_sender_t = __forge_domain::__forwarded_argument_t<
    __adapted_sender_t<__transformed_sender_t<S, Promise>>>;

template<class S, class Promise>
concept __prepared_sender_has_multiple_values = requires {
    requires (__forge_meta::__value_count<
        __sender_completions_t<__prepared_sender_t<S, Promise>, Promise>> > 1);
};

template<class S, class Promise>
concept __select_sender_bridge =
    __single_sender<S, Promise> &&
    // [exec.as.awaitable]/7.4 selects the bridge from the source expression.
    // Retain the identity fallback only for a known multi-value result.
    (!__prepared_sender_has_multiple_values<S, Promise>);

template<class Value, class Promise>
consteval bool __as_awaitable_nothrow() {
    if constexpr (__has_as_awaitable_member<Value, Promise>) {
        return noexcept(std::declval<Value>().as_awaitable(
            std::declval<Promise&>()));
    } else if constexpr (__transformed_sender_has_as_awaitable<Value, Promise>) {
        return noexcept(__adapt_for_await_completion(__transform_for_await(
            std::declval<Value>(), std::declval<Promise&>())).as_awaitable(
                std::declval<Promise&>()));
    } else if constexpr (__ordinary_awaitable<Value, Promise>) {
        return true;
    } else if constexpr (__select_sender_bridge<Value, Promise>) {
        return false;
    } else {
        return true;
    }
}

template<class S, class Promise>
using __sender_await_value_t = typename __value_from_tuple<__forge_meta::__single_value_tuple_t<
    __sender_completions_t<S, Promise>>>::type;

// Keep the receiver independent of the operation whose type connect deduces.
template<class Promise, class Value>
struct __await_state {
    using env_t = __promise_env_t<Promise>;
    using stored_value_t = std::conditional_t<
        std::is_void_v<Value>, __unit, Value>;

    explicit __await_state(Promise& promise) noexcept : __promise(std::addressof(promise)) {}

    Promise* __promise;
    std::optional<stored_value_t> __result;
    std::exception_ptr __exc;
    std::coroutine_handle<> __coro;

    struct __recv {
        using receiver_concept = receiver_tag;
        __await_state* __self;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            try {
                if constexpr (std::is_void_v<Value>) {
                    static_assert(sizeof...(Vs) == 0);
                    __self->__result.emplace();
                } else {
                    __self->__result.emplace(static_cast<Vs&&>(vs)...);
                }
            } catch (...) {
                __self->__exc = std::current_exception();
            }
            __complete(__self, __self->__coro);
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            try {
                if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
                    __self->__exc = static_cast<E&&>(e);
                } else if constexpr (std::is_same_v<std::decay_t<E>, std::error_code>) {
                    __self->__exc = std::make_exception_ptr(
                        std::system_error(static_cast<E&&>(e)));
                } else {
                    __self->__exc = std::make_exception_ptr(static_cast<E&&>(e));
                }
            } catch (...) {
                __self->__exc = std::current_exception();
            }
            __complete(__self, __self->__coro);
        }
        void set_stopped() && noexcept requires __has_unhandled_stopped<Promise> {
            auto* const key = __self;
            auto* const promise = __self->__promise;
            const auto next = static_cast<std::coroutine_handle<>>(
                promise->unhandled_stopped());
            // Completion may already have destroyed the frame. Only stack
            // values and the static deferral routine are used from here on.
            __complete(key, next);
        }
        auto get_env() const noexcept -> env_t {
            return __get_promise_env(*__self->__promise);
        }
    };

private:
    static void __complete(const void* key, std::coroutine_handle<> next) noexcept {
        if (!__start_scope::__defer(key, next)) {
            next.resume();
        }
    }
};

template<class S, class Promise>
using __await_state_t = __await_state<Promise, __sender_await_value_t<S, Promise>>;

template<class S, class Promise>
concept __awaitable_sender_connectable =
    __single_sender<S, Promise> &&
    requires {
        typename connect_result_t<S, typename __await_state_t<S, Promise>::__recv>;
    };

// An unconnectable prepared sender rejects a selected bridge; it does not
// turn the bridge into an identity fallback.
template<class S, class Promise>
concept __as_awaitable_available =
    __has_as_awaitable_member<S, Promise> ||
    __transformed_sender_has_as_awaitable<S, Promise> ||
    __ordinary_awaitable<S, Promise> ||
    (!__select_sender_bridge<S, Promise>) ||
    __awaitable_sender_connectable<__prepared_sender_t<S, Promise>, Promise>;

// With identity adaptation, the transform result is the prepared sender and
// is initialized directly in the bridge. Only the promise environment precedes it.
template<class Value, class Promise, bool = __has_await_adaptor<__transformed_sender_t<Value, Promise>>>
struct __await_chain {
    using __env_t = __promise_env_t<Promise>;
    __env_t __promise_env;

    explicit __await_chain(Value&&, Promise& promise)
        : __promise_env(__get_promise_env(promise)) {}

    decltype(auto) __prepare(Value&& value) {
        return std::execution::transform_sender(
            std::forward<Value>(value), std::forward<__env_t>(__promise_env));
    }
};

template<class Value, class Promise>
struct __await_chain<Value, Promise, true> : __await_chain<Value, Promise, false> {
    using __base = __await_chain<Value, Promise, false>;
    using __transformed_t = __transformed_sender_t<Value, Promise>;
    using __sender_env_t = decltype(std::execution::get_env(std::declval<__transformed_t&>()));
    using __adaptor_t = decltype(std::execution::get_await_completion_adaptor(
        std::declval<__sender_env_t>()));

    __transformed_t __transformed;
    __sender_env_t __sender_env;
    __adaptor_t __adaptor;

    explicit __await_chain(Value&& value, Promise& promise)
        : __base(std::forward<Value>(value), promise),
          __transformed(__base::__prepare(std::forward<Value>(value))),
          __sender_env(std::execution::get_env(__transformed)),
          __adaptor(std::execution::get_await_completion_adaptor(
              std::forward<__sender_env_t>(__sender_env))) {}

    decltype(auto) __prepare(Value&&) {
        return std::forward<__adaptor_t>(__adaptor)(std::forward<__transformed_t>(__transformed));
    }
};

// The erased chain is constructed in place and is never relocated. Its type
// is not an awaiter template argument, so it cannot contribute to co_await ADL.
template<std::size_t Size, std::size_t Align>
class __await_storage {
    alignas(Align) unsigned char __bytes[Size];
    void (*__destroy)(__await_storage&) noexcept = nullptr;

public:
    __await_storage() noexcept = default;
    __await_storage(const __await_storage&) = delete;
    __await_storage& operator=(const __await_storage&) = delete;

    template<class Make>
    explicit __await_storage(Make&& make) {
        __emplace(std::forward<Make>(make));
    }

    ~__await_storage() {
        if (__destroy) __destroy(*this);
    }

    template<class T>
    T& __get() noexcept {
        return *std::launder(static_cast<T*>(static_cast<void*>(__bytes)));
    }

    template<class Make>
    auto& __emplace(Make&& make) {
        using T = decltype(std::forward<Make>(make)());
        static_assert(sizeof(T) <= Size && alignof(T) <= Align,
                      "object size and alignment must fit the awaitable storage");
        auto* object = ::new (static_cast<void*>(__bytes)) T(std::forward<Make>(make)());
        __destroy = [](__await_storage& self) noexcept { self.template __get<T>().~T(); };
        return *object;
    }
};

template<class Fun, class... Args>
using __call_result_t = decltype(std::declval<Fun>()(std::declval<Args>()...));

} // namespace __forge_awaitable

struct as_awaitable_t;

// Keep the draft's std::execution namespace and its two associated type
// arguments. [basic.lookup.argdep]/3 excludes constant template arguments.
template<class S, class Promise, std::size_t Size, std::size_t Align>
struct __sender_awaitable {
    using value_t = __forge_awaitable::__sender_await_value_t<S, Promise>;

private:
    friend struct as_awaitable_t;

    using __state_t = __forge_awaitable::__await_state_t<S, Promise>;
    using __recv = typename __state_t::__recv;
    template<class Value>
    using __chain_t = __forge_awaitable::__await_chain<Value, Promise>;

    // Check connect's receiver-of Mandate in the receiver's actual environment.
    static_assert(__forge_receiver_of<__recv, completion_signatures_of_t<S, env_of_t<__recv>>>,
                  "awaited sender completions require a compatible promise (including unhandled_stopped)");
    using op_t = connect_result_t<S, __recv>;

    // Destruction unwinds the operation, the prepared sender, then the chain.
    __forge_awaitable::__await_storage<Size, Align> __owners;
    S __sndr;
    __state_t __state;
    __forge_awaitable::__await_storage<sizeof(op_t), alignof(op_t)> __operation;

    template<class Value>
    __sender_awaitable(Value&& value, Promise& promise)
        : __owners([&]() -> __chain_t<Value> {
              return __chain_t<Value>(std::forward<Value>(value), promise);
          }),
          __sndr(__owners.template __get<__chain_t<Value>>().__prepare(std::forward<Value>(value))),
          __state(promise) {}

public:
    // The operation and its receiver point into this awaitable.
    __sender_awaitable(const __sender_awaitable&) = delete;
    __sender_awaitable(__sender_awaitable&&) = delete;
    __sender_awaitable& operator=(const __sender_awaitable&) = delete;
    __sender_awaitable& operator=(__sender_awaitable&&) = delete;

    bool await_ready() const noexcept { return false; }

    auto await_suspend(std::coroutine_handle<Promise> h)
        -> std::coroutine_handle<> {
        auto* const state = std::addressof(__state);
        state->__coro = h;
        auto& op = __operation.__emplace([&]() -> op_t {
            return std::execution::connect(std::forward<S>(__sndr), __recv{state});
        });
        __forge_awaitable::__start_scope scope{state};
        std::execution::start(op);
        return scope.__resume;
    }

    auto await_resume() -> value_t {
        if (__state.__exc) std::rethrow_exception(__state.__exc);
        if constexpr (!std::is_void_v<value_t>) {
            return std::move(*__state.__result);
        }
    }
};

// as_awaitable(expr, promise) — [exec.as.awaitable]
struct as_awaitable_t {
    template<class Value, class Promise>
        requires __forge_awaitable::__as_awaitable_available<Value, Promise>
    [[nodiscard]] decltype(auto) operator()(Value&& value, Promise& promise) const
        noexcept(__forge_awaitable::__as_awaitable_nothrow<Value, Promise>()) {
        using namespace __forge_awaitable;

        if constexpr (__has_as_awaitable_member<Value, Promise>) {
            using result_t = decltype(static_cast<Value&&>(value).as_awaitable(promise));
            static_assert(__is_awaitable<result_t, Promise>,
                          "as_awaitable member must return an awaitable for this promise");
            return static_cast<Value&&>(value).as_awaitable(promise);
        } else if constexpr (__transformed_sender_has_as_awaitable<Value, Promise>) {
            return __adapt_for_await_completion(__transform_for_await(
                static_cast<Value&&>(value), promise)).as_awaitable(promise);
        } else if constexpr (__ordinary_awaitable<Value, Promise>) {
            return static_cast<Value&&>(value);
        } else if constexpr (__select_sender_bridge<Value, Promise>) {
            using chain_t = __await_chain<Value, Promise>;
            // __prepared_sender_t preserves lvalue references per LWG 4547.
            return __sender_awaitable<__prepared_sender_t<Value, Promise>, Promise,
                                      sizeof(chain_t), alignof(chain_t)>{
                static_cast<Value&&>(value), promise};
        } else {
            return static_cast<Value&&>(value);
        }
    }
};

inline constexpr as_awaitable_t as_awaitable{};

// with_awaitable_senders<Promise> — CRTP mixin — [exec.with.awaitable.senders]
// Makes a coroutine promise_type support co_await on senders.
template<class Promise>
    requires (std::is_class_v<Promise> && std::same_as<Promise, std::decay_t<Promise>>)
struct with_awaitable_senders {
    template<class OtherPromise>
        requires (!std::same_as<OtherPromise, void>)
    void set_continuation(std::coroutine_handle<OtherPromise> continuation) noexcept {
        __continuation = continuation;
        if constexpr (requires(OtherPromise& promise) {
                          promise.unhandled_stopped();
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
    auto await_transform(Value&& value)
        -> __forge_awaitable::__call_result_t<as_awaitable_t, Value, Promise&> {
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

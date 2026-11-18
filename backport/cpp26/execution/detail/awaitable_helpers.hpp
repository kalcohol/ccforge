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

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace std::execution::__forge_awaitable {

template<class T>
inline constexpr bool __is_coroutine_handle = false;
template<class P>
inline constexpr bool __is_coroutine_handle<std::coroutine_handle<P>> = true;

template<class T>
concept __await_suspend_result = std::same_as<T, void> ||
    std::same_as<T, bool> || __is_coroutine_handle<T>;

template<class A, class... P>
concept __is_awaiter = requires(A& a, std::coroutine_handle<P...> h) {
    a.await_ready() ? 1 : 0;
    { a.await_suspend(h) } -> __await_suspend_result;
    a.await_resume();
};

// These declarations are used only in unevaluated operands. Separate overloads
// make competing member and ADL candidates ambiguous instead of preferring one.
template<class C>
concept __member_co_await = requires(C&& c) {
    static_cast<C&&>(c).operator co_await();
};
template<class C>
concept __free_co_await = requires(C&& c) {
    operator co_await(static_cast<C&&>(c));
};
template<class C> requires __member_co_await<C>
auto __get_awaiter(C&& c) -> decltype(static_cast<C&&>(c).operator co_await());
template<class C> requires __free_co_await<C>
auto __get_awaiter(C&& c) -> decltype(operator co_await(static_cast<C&&>(c)));
template<class C> requires (!__member_co_await<C> && !__free_co_await<C>)
auto __get_awaiter(C&& c) -> C&&;

// Detect the name, including an overloaded/template or unusable await_transform.
// Promise classes used by this implementation are non-final; the final-class
// path below also recognizes callable transforms and unambiguous member names.
template<class P, bool = std::is_class_v<P> && !std::is_final_v<P>>
struct __has_await_transform_name : std::bool_constant<requires { &P::await_transform; }> {};
template<class P>
struct __has_await_transform_name<P, true> {
    struct __fallback { void await_transform(); };
    struct __probe : P, __fallback {};
    template<class U, U> struct __check;
    template<class U>
    static auto __test(__check<void (__fallback::*)(), &U::await_transform>*)
        -> std::false_type;
    template<class> static auto __test(...) -> std::true_type;
    static constexpr bool value = decltype(__test<__probe>(nullptr))::value;
};

template<class C, class P>
auto __get_awaiter(C&& c, P& p)
    -> decltype(__get_awaiter(p.await_transform(static_cast<C&&>(c))));
template<class C, class P>
    requires (!__has_await_transform_name<P>::value) &&
             (!requires(P& p, C&& c) { p.await_transform(static_cast<C&&>(c)); })
auto __get_awaiter(C&& c, P&) -> decltype(__get_awaiter(static_cast<C&&>(c)));

template<class C, class... P>
concept __is_awaitable = requires(C (*fc)() noexcept, P&... p) {
    { __get_awaiter(fc(), p...) } -> __is_awaiter<P...>;
};

template<class C, class... P>
using __await_result_t = decltype(std::declval<std::remove_reference_t<decltype(
    __get_awaiter(std::declval<C (*)() noexcept>()(), std::declval<P&>()...))>&>()
        .await_resume());

template<class T, class P>
concept __has_as_awaitable = requires(T&& t, P& p) {
    { static_cast<T&&>(t).as_awaitable(p) } -> __is_awaitable<P>;
};

template<class Derived>
struct __with_await_transform {
    template<class T>
    auto await_transform(T&& value) noexcept -> T&& {
        return static_cast<T&&>(value);
    }
    template<class T> requires __has_as_awaitable<T, Derived>
    auto await_transform(T&& value)
        noexcept(noexcept(static_cast<T&&>(value).as_awaitable(std::declval<Derived&>())))
        -> decltype(static_cast<T&&>(value).as_awaitable(std::declval<Derived&>())) {
        return static_cast<T&&>(value).as_awaitable(static_cast<Derived&>(*this));
    }
};

// Type-computation context only. Never construct or call an env-promise.
template<class Env>
struct __env_promise : __with_await_transform<__env_promise<Env>> {
    void get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void unhandled_exception() noexcept;
    void return_void() noexcept;
    std::coroutine_handle<> unhandled_stopped() noexcept;
    const Env& get_env() const noexcept;
};

} // namespace std::execution::__forge_awaitable
#endif

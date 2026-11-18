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
#include <coroutine>
#include <exception>
#include <memory>
#include <utility>

namespace std::execution::__forge_awaitable {

template<class S, class R> struct __connect_promise;

template<class S, class R>
struct __operation_state_task {
    using operation_state_concept = operation_state_tag;
    using promise_type = __connect_promise<S, R>;
    explicit __operation_state_task(std::coroutine_handle<> h) noexcept : __coro(h) {}
    __operation_state_task(__operation_state_task&& other) noexcept
        : __coro(std::exchange(other.__coro, {})) {}
    __operation_state_task(const __operation_state_task&) = delete;
    ~__operation_state_task() { if (__coro) __coro.destroy(); }
    void start() & noexcept { __coro.resume(); }
private:
    std::coroutine_handle<> __coro;
};

template<class S, class R>
struct __connect_promise : __with_await_transform<__connect_promise<S, R>> {
    __connect_promise(S&, R& r) noexcept : __rcvr(std::addressof(r)) {}
    std::suspend_always initial_suspend() noexcept { return {}; }
    [[noreturn]] std::suspend_always final_suspend() noexcept { std::terminate(); }
    [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }
    [[noreturn]] void return_void() noexcept { std::terminate(); }
    std::coroutine_handle<> unhandled_stopped() noexcept {
        std::execution::set_stopped(std::move(*__rcvr));
        return std::noop_coroutine();
    }
    auto get_return_object() noexcept -> __operation_state_task<S, R> {
        return __operation_state_task<S, R>{
            std::coroutine_handle<__connect_promise>::from_promise(*this)};
    }
    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*__rcvr);
    }
private:
    R* __rcvr;
};

template<class S, class R>
concept __connect_awaitable_available =
    sender_in<S, env_of_t<R>> &&
    __forge_receiver_of<R, completion_signatures_of_t<S, env_of_t<R>>> &&
    std::constructible_from<std::decay_t<S>, S> &&
    std::constructible_from<std::decay_t<R>, R> &&
    __is_awaitable<std::decay_t<S>, __connect_promise<std::decay_t<S>, std::decay_t<R>>> &&
    __forge_receiver_of<std::decay_t<R>, completion_signatures<
        typename __forge_cpo_detail::__await_value_signature<__await_result_t<
            std::decay_t<S>, __connect_promise<std::decay_t<S>, std::decay_t<R>>>>::type,
        set_error_t(std::exception_ptr), set_stopped_t()>>;

template<class Fun, class... Ts>
auto __suspend_complete(Fun fun, Ts&&... args) noexcept {
    auto complete = [&, fun]() noexcept { fun(static_cast<Ts&&>(args)...); };
    // Function template arguments are not associated entities of a local class.
    // User operator co_await overloads must not intercept internal completion.
    struct __completion_awaiter {
        decltype(complete) __complete;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) noexcept {
            __complete();
        }
        [[noreturn]] void await_resume() noexcept { std::terminate(); }
    };
    return __completion_awaiter{complete};
}

// Parameters are owned by the globally allocated coroutine frame. Every
// completion runs in await_suspend, after the frame has suspended.
template<class S, class R>
    requires __connect_awaitable_available<S, R>
auto __connect_awaitable(S sndr, R rcvr) -> __operation_state_task<S, R> {
    using value_t = __await_result_t<S, __connect_promise<S, R>>;
    std::exception_ptr error;
    try {
        if constexpr (std::is_void_v<value_t>) {
            co_await std::move(sndr);
            co_await __suspend_complete(std::execution::set_value, std::move(rcvr));
        } else {
            co_await __suspend_complete(std::execution::set_value, std::move(rcvr),
                                       co_await std::move(sndr));
        }
    } catch (...) {
        error = std::current_exception();
    }
    co_await __suspend_complete(std::execution::set_error, std::move(rcvr), std::move(error));
}

} // namespace std::execution::__forge_awaitable
#endif

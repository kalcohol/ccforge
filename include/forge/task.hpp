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

#include <execution>
#include <variant>
#include <exception>
#include <stdexcept>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

namespace forge {

// Forward declarations
template<class T = void> class task;

namespace __task_detail {

template<class T, class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    __op(__op&&) = delete;
    __op(const __op&) = delete;
    __op(std::coroutine_handle<typename task<T>::promise_type> coro, R r)
        : __coro(coro), __rcvr(std::move(r)) {}
    std::coroutine_handle<typename task<T>::promise_type> __coro;
    R __rcvr;

    friend void tag_invoke(std::execution::start_t, __op& self) noexcept {
        if (!self.__coro.done()) {
            self.__coro.resume();
        }
        auto& p = self.__coro.promise();
        if constexpr (!std::is_void_v<T>) {
            if (p.result.index() == 2) {
                std::execution::set_error(std::move(self.__rcvr), std::get<2>(p.result));
            } else if (p.result.index() == 1) {
                std::execution::set_value(std::move(self.__rcvr),
                    std::move(std::get<1>(p.result)));
            } else {
                std::execution::set_error(std::move(self.__rcvr),
                    std::make_exception_ptr(std::runtime_error("task: no result")));
            }
        } else {
            if (p.exc_) {
                std::execution::set_error(std::move(self.__rcvr), p.exc_);
            } else {
                std::execution::set_value(std::move(self.__rcvr));
            }
        }
    }
};

} // namespace __task_detail

template<class T>
class task {
public:
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        std::variant<std::monostate, T, std::exception_ptr> result;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void return_value(T val) { result.template emplace<1>(std::move(val)); }
        void unhandled_exception() noexcept { result.template emplace<2>(std::current_exception()); }
    };

    using sender_concept = std::execution::sender_t;
    task(task&&) noexcept = default;
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    ~task() { if (__coro_ && __coro_.done()) __coro_.destroy(); }

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const task&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(T),
            std::execution::set_error_t(std::exception_ptr)> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t, const task&) noexcept
        -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, task self, R r)
        -> __task_detail::__op<T, R>
    {
        return __task_detail::__op<T, R>{self.__coro_, std::move(r)};
    }

    std::coroutine_handle<promise_type> __coro_;

private:
    explicit task(std::coroutine_handle<promise_type> coro) noexcept
        : __coro_(coro) {}
};

template<>
class task<void> {
public:
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        bool done_ = false;
        std::exception_ptr exc_;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void return_void() noexcept { done_ = true; }
        void unhandled_exception() noexcept { exc_ = std::current_exception(); }
    };

    using sender_concept = std::execution::sender_t;
    task(task&&) noexcept = default;
    task(const task&) = delete;
    ~task() { if (__coro_ && __coro_.done()) __coro_.destroy(); }

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const task&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr)> {
        return {};
    }
    friend auto tag_invoke(std::execution::get_env_t, const task&) noexcept
        -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, task self, R r)
        -> __task_detail::__op<void, R>
    {
        return __task_detail::__op<void, R>{self.__coro_, std::move(r)};
    }

    std::coroutine_handle<promise_type> __coro_;

private:
    explicit task(std::coroutine_handle<promise_type> coro) noexcept : __coro_(coro) {}
};

} // namespace forge

#endif // __cpp_impl_coroutine

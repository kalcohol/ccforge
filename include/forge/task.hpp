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
#include <type_traits>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

namespace forge {

// Forward declarations
template<class T = void> class task;

namespace __task_detail {

struct __op_base {
    virtual void __complete() noexcept = 0;
    virtual ~__op_base() = default;
};

struct __final_awaiter {
    bool await_ready() noexcept { return false; }

    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> coro) noexcept {
        if (auto* op = coro.promise().__op_) {
            op->__complete();
        }
    }

    void await_resume() noexcept {}
};

template<class T, class R>
struct __op : __op_base {
    using operation_state_concept = std::execution::operation_state_t;
    __op(__op&&) = delete;
    __op(const __op&) = delete;
    __op(std::coroutine_handle<typename task<T>::promise_type> coro, R r)
        : __coro(coro), __rcvr(std::move(r)) {}
    ~__op() {
        if (__coro) {
            __coro.promise().__op_ = nullptr;
            __coro.destroy();
        }
    }
    std::coroutine_handle<typename task<T>::promise_type> __coro;
    R __rcvr;

    void start() & noexcept {
        __coro.promise().__op_ = this;
        __coro.resume();
    }

    void __complete() noexcept override {
        if (__completed) {
            return;
        }
        __completed = true;

        auto& p = __coro.promise();
        if (p.stopped_) {
            std::execution::set_stopped(std::move(__rcvr));
            return;
        }
        if constexpr (!std::is_void_v<T>) {
            if (p.result.index() == 2) {
                std::execution::set_error(std::move(__rcvr), std::get<2>(p.result));
            } else if (p.result.index() == 1) {
                std::execution::set_value(std::move(__rcvr),
                    std::move(std::get<1>(p.result)));
            } else {
                std::execution::set_error(std::move(__rcvr),
                    std::make_exception_ptr(std::runtime_error("task: no result")));
            }
        } else {
            if (p.exc_) {
                std::execution::set_error(std::move(__rcvr), p.exc_);
            } else {
                std::execution::set_value(std::move(__rcvr));
            }
        }
    }

    bool __completed = false;
};

} // namespace __task_detail

template<class T>
class task {
public:
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        std::variant<std::monostate, T, std::exception_ptr> result;
        bool stopped_ = false;
        __task_detail::__op_base* __op_ = nullptr;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        __task_detail::__final_awaiter final_suspend() noexcept { return {}; }
        void return_value(T val) { result.template emplace<1>(std::move(val)); }
        void unhandled_exception() noexcept { result.template emplace<2>(std::current_exception()); }
        std::coroutine_handle<> unhandled_stopped() noexcept {
            stopped_ = true;
            if (__op_) {
                __op_->__complete();
            }
            return std::noop_coroutine();
        }
    };

    using sender_concept = std::execution::sender_t;
    task(task&& other) noexcept : __coro_(std::exchange(other.__coro_, {})) {}
    task(const task&) = delete;
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (__coro_) __coro_.destroy();
            __coro_ = std::exchange(other.__coro_, {});
        }
        return *this;
    }
    task& operator=(const task&) = delete;
    ~task() { if (__coro_) __coro_.destroy(); }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(T),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) &&
        -> __task_detail::__op<T, R>
    {
        return __task_detail::__op<T, R>{std::exchange(__coro_, {}), std::move(r)};
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
        __task_detail::__op_base* __op_ = nullptr;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        __task_detail::__final_awaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept { done_ = true; }
        void unhandled_exception() noexcept { exc_ = std::current_exception(); }
        std::coroutine_handle<> unhandled_stopped() noexcept {
            stopped_ = true;
            if (__op_) {
                __op_->__complete();
            }
            return std::noop_coroutine();
        }
        bool stopped_ = false;
    };

    using sender_concept = std::execution::sender_t;
    task(task&& other) noexcept : __coro_(std::exchange(other.__coro_, {})) {}
    task(const task&) = delete;
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (__coro_) __coro_.destroy();
            __coro_ = std::exchange(other.__coro_, {});
        }
        return *this;
    }
    task& operator=(const task&) = delete;
    ~task() { if (__coro_) __coro_.destroy(); }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R r) &&
        -> __task_detail::__op<void, R>
    {
        return __task_detail::__op<void, R>{std::exchange(__coro_, {}), std::move(r)};
    }

    std::coroutine_handle<promise_type> __coro_;

private:
    explicit task(std::coroutine_handle<promise_type> coro) noexcept : __coro_(coro) {}
};

} // namespace forge

#endif // __cpp_impl_coroutine

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

#include "any_scheduler.hpp"
#include "any_stop_token.hpp"

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

// Fallback start scheduler used when the receiver connected to the task does
// not advertise get_start_scheduler. Scheduling on it completes inline on the
// thread that starts the schedule operation, so awaited algorithms that
// reschedule onto the start scheduler (such as counting-scope join) complete
// wherever the awaited work finished, matching the task's pre-scheduler
// behavior.
class __inline_fallback_scheduler;

namespace __inline_fallback {

struct __env {
    friend auto tag_invoke(
        std::execution::get_completion_scheduler_t<std::execution::set_value_t>,
        const __env&) noexcept -> __inline_fallback_scheduler;
};

template<class R>
struct __schedule_op {
    using operation_state_concept = std::execution::operation_state_t;

    __schedule_op(__schedule_op&&) = delete;
    __schedule_op& operator=(__schedule_op&&) = delete;

    explicit __schedule_op(R rcvr) : __rcvr_(std::move(rcvr)) {}

    void start() & noexcept {
        std::execution::set_value(std::move(__rcvr_));
    }

    [[no_unique_address]] R __rcvr_;
};

struct __schedule_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const -> __schedule_op<R> {
        return __schedule_op<R>(std::move(rcvr));
    }

    auto get_env() const noexcept -> __env { return {}; }
};

} // namespace __inline_fallback

class __inline_fallback_scheduler {
public:
    using scheduler_concept = std::execution::scheduler_t;

    __inline_fallback_scheduler() noexcept = default;

    [[nodiscard]] auto schedule() const noexcept
        -> __inline_fallback::__schedule_sender {
        return {};
    }

    bool operator==(const __inline_fallback_scheduler&) const noexcept = default;
};

namespace __inline_fallback {
inline auto tag_invoke(
    std::execution::get_completion_scheduler_t<std::execution::set_value_t>,
    const __env&) noexcept -> __inline_fallback_scheduler {
    return {};
}
} // namespace __inline_fallback

// Environment exposed to senders awaited inside the task body. The start
// scheduler is borrowed from the receiver the task itself was connected to
// (type-erased), so schedule-requiring awaited algorithms observe the same
// start scheduler the surrounding task operation was started with.
struct __env {
    any_stop_token __token;
    forge::any_scheduler __scheduler;

    [[nodiscard]] auto query(std::execution::get_stop_token_t) const noexcept
        -> any_stop_token {
        return __token;
    }

    [[nodiscard]] auto query(std::execution::get_start_scheduler_t) const noexcept
        -> forge::any_scheduler {
        return __scheduler;
    }
};

template<class OuterEnv>
auto __capture_start_scheduler(const OuterEnv& env) -> forge::any_scheduler {
    if constexpr (requires {
        forge::any_scheduler{std::execution::get_start_scheduler(env)};
    }) {
        return forge::any_scheduler{std::execution::get_start_scheduler(env)};
    } else {
        return forge::any_scheduler{__inline_fallback_scheduler{}};
    }
}

struct __final_awaiter {
    bool await_ready() noexcept { return false; }

    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> coro) noexcept {
        // Completion runs while the coroutine frame is still on the resume stack.
        // Receivers must not synchronously destroy this task operation state here.
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
    // Throwing-move T is supported: __complete passes the stored result to
    // the receiver by reference (no T is constructed in the call
    // expression), and receivers that materialize the value do so in their
    // own bodies where they route a throw to set_error. The move into the
    // promise's result variant happens inside the coroutine body, where a
    // throw lands in unhandled_exception.
public:
    struct promise_type : std::execution::with_awaitable_senders<promise_type> {
        std::variant<std::monostate, T, std::exception_ptr> result;
        any_stop_token __stop_token_;
        any_scheduler __start_scheduler_;
        bool stopped_ = false;
        __task_detail::__op_base* __op_ = nullptr;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        __task_detail::__final_awaiter final_suspend() noexcept { return {}; }
        void return_value(T val) { result.template emplace<1>(std::move(val)); }
        void unhandled_exception() noexcept {
            if (!stopped_) {
                result.template emplace<2>(std::current_exception());
            }
        }
        std::coroutine_handle<> unhandled_stopped() noexcept {
            stopped_ = true;
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
        auto get_env() const noexcept -> __task_detail::__env {
            return __task_detail::__env{__stop_token_, __start_scheduler_};
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
        if (!__coro_) {
            throw std::logic_error{"task: empty"};
        }
        auto env = std::execution::get_env(r);
        auto token = std::execution::get_stop_token(env);
        any_stop_token erased_token;
        if constexpr (!std::unstoppable_token<std::remove_cvref_t<decltype(token)>>) {
            erased_token = any_stop_token{std::move(token)};
        }
        __coro_.promise().__stop_token_ = std::move(erased_token);
        __coro_.promise().__start_scheduler_ =
            __task_detail::__capture_start_scheduler(env);
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
        any_stop_token __stop_token_;
        any_scheduler __start_scheduler_;
        __task_detail::__op_base* __op_ = nullptr;

        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        __task_detail::__final_awaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept { done_ = true; }
        void unhandled_exception() noexcept {
            if (!stopped_) {
                exc_ = std::current_exception();
            }
        }
        std::coroutine_handle<> unhandled_stopped() noexcept {
            stopped_ = true;
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
        auto get_env() const noexcept -> __task_detail::__env {
            return __task_detail::__env{__stop_token_, __start_scheduler_};
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
        if (!__coro_) {
            throw std::logic_error{"task: empty"};
        }
        auto env = std::execution::get_env(r);
        auto token = std::execution::get_stop_token(env);
        any_stop_token erased_token;
        if constexpr (!std::unstoppable_token<std::remove_cvref_t<decltype(token)>>) {
            erased_token = any_stop_token{std::move(token)};
        }
        __coro_.promise().__stop_token_ = std::move(erased_token);
        __coro_.promise().__start_scheduler_ =
            __task_detail::__capture_start_scheduler(env);
        return __task_detail::__op<void, R>{std::exchange(__coro_, {}), std::move(r)};
    }

    std::coroutine_handle<promise_type> __coro_;

private:
    explicit task(std::coroutine_handle<promise_type> coro) noexcept : __coro_(coro) {}
};

} // namespace forge

#endif // __cpp_impl_coroutine

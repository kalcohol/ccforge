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

#include <forge/any_scheduler.hpp>
#include <forge/resource_policy.hpp>

#include <concepts>
#include <exception>
#include <execution>
#include <memory_resource>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif

namespace forge::io::experimental {

class executor_ref {
public:
    executor_ref() = default;

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, executor_ref>)
              && (!std::is_same_v<std::remove_cvref_t<Scheduler>, forge::any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    executor_ref(Scheduler&& scheduler)
        : scheduler_(static_cast<Scheduler&&>(scheduler))
    {}

    executor_ref(forge::any_scheduler scheduler) noexcept
        : scheduler_(std::move(scheduler))
    {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(scheduler_);
    }

    [[nodiscard]] auto schedule() const noexcept {
        return scheduler_.schedule();
    }

    [[nodiscard]] auto scheduler() const noexcept -> const forge::any_scheduler& {
        return scheduler_;
    }

private:
    forge::any_scheduler scheduler_{};
};

struct io_env {
    executor_ref executor{};
    std::stop_token stop_token{};
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace __coro_detail {

template<class T>
concept await_suspend_result =
    std::same_as<T, void> ||
    std::same_as<T, bool> ||
    std::convertible_to<T, std::coroutine_handle<>>;

} // namespace __coro_detail

template<class Awaitable>
concept io_awaitable =
    requires(Awaitable& awaitable,
             std::coroutine_handle<> continuation,
             const io_env* env) {
        { awaitable.await_ready() } -> std::convertible_to<bool>;
        { awaitable.await_suspend(continuation, env) }
            -> __coro_detail::await_suspend_result;
        awaitable.await_resume();
    };

namespace __coro_detail {

template<class Awaitable>
class env_await_adapter {
public:
    explicit env_await_adapter(Awaitable awaitable, const io_env** env) noexcept(
        std::is_nothrow_move_constructible_v<Awaitable>)
        : awaitable_(std::move(awaitable))
        , env_(env)
    {}

    [[nodiscard]] auto await_ready() noexcept(noexcept(awaitable_.await_ready()))
        -> bool {
        return awaitable_.await_ready();
    }

    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> continuation)
        noexcept(noexcept(awaitable_.await_suspend(continuation, *env_))) {
        return awaitable_.await_suspend(continuation, *env_);
    }

    auto await_resume() noexcept(noexcept(awaitable_.await_resume()))
        -> decltype(auto) {
        return awaitable_.await_resume();
    }

private:
    Awaitable awaitable_;
    const io_env** env_;
};

template<class Promise>
struct promise_base {
    const io_env* env = nullptr;

    [[nodiscard]] auto initial_suspend() noexcept -> std::suspend_always {
        return {};
    }

    [[nodiscard]] auto final_suspend() noexcept -> std::suspend_always {
        return {};
    }

    template<class Awaitable>
        requires io_awaitable<std::remove_cvref_t<Awaitable>>
    [[nodiscard]] auto await_transform(Awaitable&& awaitable) {
        using awaitable_t = std::remove_cvref_t<Awaitable>;
        return env_await_adapter<awaitable_t>{
            awaitable_t{static_cast<Awaitable&&>(awaitable)},
            &env};
    }

    template<class Awaitable>
        requires (!io_awaitable<std::remove_cvref_t<Awaitable>>)
    [[nodiscard]] auto await_transform(Awaitable&& awaitable) {
        using awaitable_t = std::remove_cvref_t<Awaitable>;
        return awaitable_t{static_cast<Awaitable&&>(awaitable)};
    }
};

template<class T>
using task_result_storage = std::variant<std::monostate, T, std::exception_ptr>;

} // namespace __coro_detail

class read_env_awaitable {
public:
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(std::coroutine_handle<>, const io_env* env) noexcept
        -> bool {
        env_ = env;
        return false;
    }

    [[nodiscard]] auto await_resume() const -> const io_env& {
        if (env_ == nullptr) {
            throw std::logic_error{"forge::io::experimental::io_env is not set"};
        }
        return *env_;
    }

private:
    const io_env* env_ = nullptr;
};

[[nodiscard]] inline auto this_io_env() noexcept -> read_env_awaitable {
    return {};
}

template<class T>
class io_task {
public:
    struct promise_type : __coro_detail::promise_base<promise_type> {
        __coro_detail::task_result_storage<T> result{};

        [[nodiscard]] auto get_return_object() noexcept -> io_task {
            return io_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        template<class U>
            requires std::constructible_from<T, U&&>
        auto return_value(U&& value) -> void {
            result.template emplace<1>(static_cast<U&&>(value));
        }

        auto unhandled_exception() noexcept -> void {
            result.template emplace<2>(std::current_exception());
        }
    };

    io_task(io_task&& other) noexcept
        : coro_(std::exchange(other.coro_, {}))
    {}

    auto operator=(io_task&& other) noexcept -> io_task& {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    io_task(const io_task&) = delete;
    auto operator=(const io_task&) -> io_task& = delete;

    ~io_task() {
        if (coro_) {
            coro_.destroy();
        }
    }

    auto start(const io_env& env) -> void {
        if (!coro_ || coro_.done()) {
            return;
        }
        coro_.promise().env = &env;
        coro_.resume();
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !coro_ || coro_.done();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(coro_);
    }

    [[nodiscard]] auto result() && -> T {
        if (!done()) {
            throw std::logic_error{"forge::io::experimental::io_task is not done"};
        }

        auto& storage = coro_.promise().result;
        if (storage.index() == 2) {
            std::rethrow_exception(std::get<2>(storage));
        }
        if (storage.index() != 1) {
            throw std::logic_error{"forge::io::experimental::io_task has no result"};
        }
        return std::move(std::get<1>(storage));
    }

private:
    explicit io_task(std::coroutine_handle<promise_type> coro) noexcept
        : coro_(coro)
    {}

    std::coroutine_handle<promise_type> coro_{};
};

template<>
class io_task<void> {
public:
    struct promise_type : __coro_detail::promise_base<promise_type> {
        bool returned = false;
        std::exception_ptr error{};

        [[nodiscard]] auto get_return_object() noexcept -> io_task {
            return io_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto return_void() noexcept -> void {
            returned = true;
        }

        auto unhandled_exception() noexcept -> void {
            error = std::current_exception();
        }
    };

    io_task(io_task&& other) noexcept
        : coro_(std::exchange(other.coro_, {}))
    {}

    auto operator=(io_task&& other) noexcept -> io_task& {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    io_task(const io_task&) = delete;
    auto operator=(const io_task&) -> io_task& = delete;

    ~io_task() {
        if (coro_) {
            coro_.destroy();
        }
    }

    auto start(const io_env& env) -> void {
        if (!coro_ || coro_.done()) {
            return;
        }
        coro_.promise().env = &env;
        coro_.resume();
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !coro_ || coro_.done();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(coro_);
    }

    auto result() && -> void {
        if (!done()) {
            throw std::logic_error{"forge::io::experimental::io_task is not done"};
        }

        if (coro_.promise().error) {
            std::rethrow_exception(coro_.promise().error);
        }
        if (!coro_.promise().returned) {
            throw std::logic_error{"forge::io::experimental::io_task has no result"};
        }
    }

private:
    explicit io_task(std::coroutine_handle<promise_type> coro) noexcept
        : coro_(coro)
    {}

    std::coroutine_handle<promise_type> coro_{};
};

#endif // __cpp_impl_coroutine

} // namespace forge::io::experimental

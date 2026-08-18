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

#include <forge/io/coro.hpp>
#include <forge/io/error.hpp>
#include <forge/io/result.hpp>
#include <forge/timer_context.hpp>

#include <chrono>
#include <exception>
#include <system_error>
#include <utility>

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace __timer_await_detail {

[[nodiscard]] inline auto exception_code(std::exception_ptr exception)
    -> std::error_code {
    auto typed = forge::io::typed_detail::from_exception(std::move(exception));
    if (typed.code) {
        return typed.code;
    }
    return std::make_error_code(std::errc::io_error);
}

template<class Sender>
auto await_timer_result(Sender sender) -> io_task<io_result<>> {
    try {
        co_await await_sender(std::move(sender));
        co_return io_result<>::success();
    } catch (const sender_stopped&) {
        throw;
    } catch (...) {
        co_return io_result<>::failure(
            exception_code(std::current_exception()));
    }
}

} // namespace __timer_await_detail

template<class Rep, class Period>
[[nodiscard]] auto async_sleep_for(
    forge::timer_context& context,
    std::chrono::duration<Rep, Period> delay) -> io_task<io_result<>> {
    return __timer_await_detail::await_timer_result(
        context.schedule_after(delay));
}

template<class Clock, class Duration>
[[nodiscard]] auto async_sleep_until(
    forge::timer_context& context,
    std::chrono::time_point<Clock, Duration> deadline) -> io_task<io_result<>> {
    return __timer_await_detail::await_timer_result(
        context.schedule_at(deadline));
}

#endif // __cpp_impl_coroutine

} // namespace forge::io


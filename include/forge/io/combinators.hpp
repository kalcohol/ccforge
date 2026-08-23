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
#include <forge/io/result.hpp>
#include <forge/io/timer_await.hpp>

#include <chrono>
#include <cstddef>
#include <execution>
#include <optional>
#include <system_error>
#include <utility>

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

template<class First, class Second>
struct when_all_result {
    std::optional<First> first;
    std::optional<Second> second;
};

template<class First, class Second>
struct when_any_result {
    std::optional<First> first;
    std::optional<Second> second;
    std::size_t winner = 0;
};

#endif // __cpp_impl_coroutine

} // namespace forge::io

#include <forge/io/detail/when_all_results.hpp>
#include <forge/io/detail/when_any_results.hpp>

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

template<class First, class Second>
    requires __io_combinator_detail::is_io_result_v<First> &&
             __io_combinator_detail::is_io_result_v<Second>
[[nodiscard]] auto when_all_results(
    io_task<First> first,
    io_task<Second> second,
    io_env env = {}) {
    return __io_combinator_detail::when_all_sender<First, Second>{
        std::move(first),
        std::move(second),
        std::move(env)};
}

template<class First, class Second>
    requires __io_combinator_detail::is_io_result_v<First> &&
             __io_combinator_detail::is_io_result_v<Second>
[[nodiscard]] auto when_any_results(
    io_task<First> first,
    io_task<Second> second,
    io_env env = {}) {
    return __io_combinator_detail::when_any_sender<First, Second>{
        std::move(first),
        std::move(second),
        std::move(env)};
}

template<class Result, class Rep, class Period>
    requires __io_combinator_detail::is_io_result_v<Result>
[[nodiscard]] auto with_timeout(
    io_task<Result> task,
    std::chrono::duration<Rep, Period> timeout,
    forge::timer_context& timers,
    io_env env = {}) {
    using timer_result_t = io_result<>;
    using payload_t = when_any_result<Result, timer_result_t>;
    using aggregate_t = io_result<payload_t>;

    return std::execution::then(
        when_any_results(
            std::move(task),
            async_sleep_for(timers, timeout),
            std::move(env)),
        [](aggregate_t result) -> aggregate_t {
            if (result.status() == io_status::value &&
                get<1>(result).winner == 1) {
                auto payload = std::move(get<1>(result));
                return aggregate_t::failure(
                    std::make_error_code(std::errc::timed_out),
                    std::move(payload));
            }
            return std::move(result);
        });
}

#endif // __cpp_impl_coroutine

} // namespace forge::io

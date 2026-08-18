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

#include <forge/io/combinators.hpp>
#include <forge/io/memory_stream.hpp>

#include "example_support.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <execution>
#include <iostream>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io;
using namespace std::chrono_literals;

auto read_now(
    cio::scripted_read_stream& stream,
    std::array<std::byte, 4>& buffer)
    -> cio::io_task<cio::io_result<std::size_t>> {
    co_return stream.read_some(
        cio::mutable_buffer{buffer.data(), buffer.size()});
}

auto read_after(
    cio::scripted_read_stream& stream,
    std::array<std::byte, 4>& buffer,
    forge::timer_context& timers,
    std::chrono::milliseconds delay)
    -> cio::io_task<cio::io_result<std::size_t>> {
    co_await cio::async_sleep_for(timers, delay);
    co_return stream.read_some(
        cio::mutable_buffer{buffer.data(), buffer.size()});
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::timer_context timers;
    cio::scripted_read_stream fast_stream{
        cio::scripted_read_step::bytes("fast")};
    std::array<std::byte, 4> fast_buffer{};

    auto fast = std::this_thread::sync_wait(
        cio::with_timeout(
            read_now(fast_stream, fast_buffer),
            1s,
            timers));
    forge_example::require(fast.has_value());
    auto [fast_aggregate] = std::move(*fast);
    forge_example::require(fast_aggregate.has_value());
    auto& fast_payload = cio::get<1>(fast_aggregate);
    forge_example::require(fast_payload.winner == 0u);
    forge_example::require(fast_payload.first.has_value());
    forge_example::require(cio::get<1>(*fast_payload.first) == 4u);

    cio::scripted_read_stream slow_stream{
        cio::scripted_read_step::bytes("slow")};
    std::array<std::byte, 4> slow_buffer{};
    auto slow = std::this_thread::sync_wait(
        cio::with_timeout(
            read_after(slow_stream, slow_buffer, timers, 20ms),
            1ms,
            timers));
    timers.wait();

    forge_example::require(slow.has_value());
    auto [slow_aggregate] = std::move(*slow);
    forge_example::require(
        slow_aggregate.error() ==
        std::make_error_code(std::errc::timed_out));
    forge_example::require(cio::get<1>(slow_aggregate).winner == 1u);

    std::cout << "coro read completed, then timed out as expected\n";
#else
    std::cout << "coro timeout unavailable\n";
#endif
}


// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include <forge/io/coro.hpp>
#include <forge/static_thread_pool.hpp>

#include "example_support.hpp"

#include <execution>
#include <iostream>
#include <thread>
#include <tuple>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io;

auto hop_to_executor(
    std::thread::id* before,
    std::thread::id* after) -> cio::io_task<bool> {
    *before = std::this_thread::get_id();
    const auto& env = co_await cio::this_io_env();
    co_await cio::await_sender(env.executor.schedule());
    *after = std::this_thread::get_id();
    co_return *after != *before;
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::static_thread_pool pool{1};
    cio::io_env env;
    env.executor = cio::executor_ref{pool.get_scheduler()};

    std::thread::id before;
    std::thread::id after;
    auto result = std::execution::sync_wait(
        cio::as_sender(hop_to_executor(&before, &after), env));
    pool.wait();

    forge_example::require(result.has_value());
    forge_example::require(std::get<0>(*result));
    forge_example::require(before != after);

    std::cout << "coro executor hop observed\n";
#else
    std::cout << "coro executor hop unavailable\n";
#endif
}

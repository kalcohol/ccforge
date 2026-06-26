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
#include <memory_resource>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io;

auto observe_env(std::pmr::memory_resource* expected) -> cio::io_task<bool> {
    const auto& env = co_await cio::this_io_env();
    co_return static_cast<bool>(env.executor)
        && env.stop_token.stop_requested()
        && env.memory == expected;
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::static_thread_pool pool{1};
    std::inplace_stop_source stop;
    stop.request_stop();
    std::pmr::monotonic_buffer_resource memory;

    cio::io_env env;
    env.executor = cio::executor_ref{pool.get_scheduler()};
    env.stop_token = stop.get_token();
    env.memory = &memory;

    auto task = observe_env(&memory);
    task.start(env);
    forge_example::require(task.done());
    forge_example::require(std::move(task).result());

    auto scheduled = std::execution::sync_wait(env.executor.schedule());
    pool.wait();
    forge_example::require(scheduled.has_value());

    std::cout << "coro io env observed\n";
#else
    std::cout << "coro io env unavailable\n";
#endif
}

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

#include <forge/async_scope.hpp>
#include <forge/static_thread_pool.hpp>
#include <execution>
#include <atomic>
#include "example_support.hpp"

int main() {
    forge::static_thread_pool pool{2};
    forge::async_scope scope;
    std::atomic<int> count{0};

    bool spawned = scope.spawn(
        std::execution::schedule(pool.get_scheduler()) |
        std::execution::then([&] noexcept {
            count.fetch_add(1, std::memory_order_relaxed);
        }));

    forge_example::require(spawned);
    scope.wait();
    pool.wait();
    forge_example::require(count.load(std::memory_order_relaxed) == 1);

    scope.close();
    forge_example::require(!scope.spawn(std::execution::just()));
}


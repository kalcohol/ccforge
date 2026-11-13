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

// Demonstrates: execution spawn future
// Key APIs: std::execution::simple_counting_scope, std::execution::spawn, std::execution::just, std::execution::spawn_future
// Requires: SENDERS: injected backport or complete native ; C++23
// See also: docs/backports/execution.md

#include <execution>

#include "example_support.hpp"
#include <iostream>
#include <tuple>
#include <utility>

namespace ex = std::execution;

int main() {
    ex::simple_counting_scope scope;
    auto token = scope.get_token();

    ex::spawn(ex::just(), token);

    auto future = ex::spawn_future(
        ex::just(21) | ex::then([](int value) noexcept {
            return value * 2;
        }),
        token);

    auto result = std::this_thread::sync_wait(std::move(future));
    forge_example::require(result.has_value());
    forge_example::require(std::get<0>(*result) == 42);

    scope.close();
    auto join_result = std::this_thread::sync_wait(scope.join());
    forge_example::require(join_result.has_value());

    std::cout << "spawn_future=" << std::get<0>(*result) << '\n';
    return 0;
}

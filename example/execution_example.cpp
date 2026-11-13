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

// Demonstrates: execution
// Key APIs: std::execution::just, std::execution::then
// Requires: SENDERS: injected backport or complete native ; C++23
// See also: docs/backports/execution.md

#include <execution>

#include "example_support.hpp"

#include <iostream>
#include <tuple>
#include <utility>

int main() {
    auto sender =
        std::execution::just(10) |
        std::execution::then([](int v) { return v + 1; });

    auto result = std::this_thread::sync_wait(std::move(sender));
    if (!static_cast<bool>(result)) {
        return 1;
    }
    forge_example::require(std::get<0>(*result) == 11);

    std::cout << std::get<0>(*result) << '\n';
    return 0;
}

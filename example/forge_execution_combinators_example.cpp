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

// Demonstrates: cached sender results and serial bulk work
// Key APIs: forge::split, forge::bulk, std::execution::just, std::execution::then
// Requires: Forge extensions and sender support; C++23
// See also: docs/backports/execution.md

#include <forge/bulk.hpp>
#include <forge/split.hpp>
#include <execution>
#include "example_support.hpp"
#include <tuple>
#include <utility>

namespace ex = std::execution;

int main() {
    // split: reuse a single sender result
    auto shared = forge::split(ex::just(100) | ex::then([](int x) { return x * 2; }));
    auto first_shared = std::this_thread::sync_wait(shared);
    auto second_shared = std::this_thread::sync_wait(shared);
    forge_example::require(first_shared.has_value());
    forge_example::require(second_shared.has_value());
    forge_example::require(std::get<0>(*first_shared) == 200);
    forge_example::require(std::get<0>(*second_shared) == 200);

    // Match the availability condition for forge::bulk in <forge/bulk.hpp>.
#if (defined(FORGE_EXECUTION_BACKPORT_ACTIVE) || \
     (defined(FORGE_NATIVE_SENDERS_COMPLETE) && FORGE_NATIVE_SENDERS_COMPLETE) || \
     (!defined(FORGE_NATIVE_SENDERS_COMPLETE) && defined(__cpp_lib_senders) && __cpp_lib_senders >= 202506L)) && \
    ((defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) && FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) || \
     (!defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) && \
      defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L))
    // bulk: serial iteration
    int sum = 0;
    auto bulk_work = ex::just(0) | forge::bulk(5, [&sum](int i, int&) { sum += i; });
    auto bulk_result = std::this_thread::sync_wait(std::move(bulk_work));
    forge_example::require(bulk_result.has_value());
    forge_example::require(sum == 10);
#endif

    return 0;
}

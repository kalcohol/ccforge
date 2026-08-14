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

#include <execution>
#include "example_support.hpp"

#include <atomic>
#include <iostream>

int main() {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    std::atomic<bool> task_ran = false;

    std::execution::spawn(
        std::execution::just()
            | std::execution::then([&] noexcept {
                  task_ran.store(true, std::memory_order_release);
              }),
        token);

    scope.close();
    auto join_result = std::this_thread::sync_wait(scope.join());

    forge_example::require(join_result.has_value());
    forge_example::require(task_ran.load(std::memory_order_acquire));
    forge_example::require(scope.count() == 0);
    std::cout << "scope_count=" << scope.count() << '\n';
    return 0;
}

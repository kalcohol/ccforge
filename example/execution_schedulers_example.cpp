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
#include <iostream>
#include <thread>
#include <tuple>

int main() {
    auto inline_result = std::this_thread::sync_wait(
        std::execution::schedule(std::execution::inline_scheduler{}) |
        std::execution::then([] { return 1; })
    );
    forge_example::require(inline_result.has_value());
    forge_example::require(std::get<0>(*inline_result) == 1);
    std::execution::run_loop loop;
    auto run_loop_sender =
        std::execution::starts_on(loop.get_scheduler(), std::execution::just()) |
        std::execution::then([&] {
            std::cout << "inline=" << std::get<0>(*inline_result) << ", run_loop=1\n";
        });
    std::thread runner{[&] { loop.run(); }};
    auto run_loop_result =
        std::this_thread::sync_wait(std::move(run_loop_sender));
    loop.finish();
    runner.join();
    forge_example::require(run_loop_result.has_value());
    return 0;
}

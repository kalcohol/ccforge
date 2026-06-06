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

#include <forge/single_thread_context.hpp>
#include <cassert>
#include "example_support.hpp"
#include <thread>
#include <tuple>

int main() {
    forge::single_thread_context ctx;
    auto sch = ctx.get_scheduler();
    auto first = std::execution::sync_wait(
        std::execution::schedule(sch)
            | std::execution::then([] { return std::this_thread::get_id(); }));
    auto second = std::execution::sync_wait(
        std::execution::schedule(sch)
            | std::execution::then([] { return std::this_thread::get_id(); }));

    forge_example::require(first && second);
    forge_example::require(std::get<0>(*first) == std::get<0>(*second));
}

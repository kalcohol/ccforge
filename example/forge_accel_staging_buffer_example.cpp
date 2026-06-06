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

#include <forge/accel.hpp>

#include <execution>
#include <algorithm>
#include <cassert>
#include "example_support.hpp"
#include <initializer_list>
#include <tuple>

int main() {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();

    forge::accel::mock::host_buffer<int> input{ctx, 4};
    forge::accel::mock::host_buffer<int> output{ctx, 4};
    forge::accel::mock::device_buffer<int> device{ctx, 4};

    std::ranges::copy(std::initializer_list<int>{1, 2, 3, 4}, input.span().begin());

    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, device, input.span())).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] {
            for (auto& value : device.span()) {
                value += 10;
            }
        })).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, output.span(), device)).has_value());

    forge_example::require(output.span()[0] == 11);
    forge_example::require(output.span()[1] == 12);
    forge_example::require(output.span()[2] == 13);
    forge_example::require(output.span()[3] == 14);
}

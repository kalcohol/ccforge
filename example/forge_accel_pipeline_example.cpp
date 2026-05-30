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
#include <numeric>
#include <span>
#include <tuple>
#include <vector>

int main() {
    forge::accel::context ctx{forge::accel::context_options{
        .thread_count = 1,
        .queue_capacity = 8,
    }};
    auto q = ctx.get_queue();

    std::vector<float> input{1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> output(input.size());
    forge::accel::device_buffer<float> device{ctx, input.size()};

    assert(std::execution::sync_wait(
        forge::accel::copy_to_device(q, device, std::span<const float>{input})).has_value());

    assert(std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            for (auto& value : device.span()) {
                value = value * 2.0f + 1.0f;
            }
        })).has_value());

    auto result = std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<float>{output}, device)
        | std::execution::then([&] {
            return std::accumulate(output.begin(), output.end(), 0.0f);
        }));

    assert(result.has_value());
    assert(std::get<0>(*result) == 24.0f);
}

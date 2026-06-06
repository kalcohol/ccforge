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

#include <simd>

#include <algorithm>
#include <cassert>
#include "example_support.hpp"
#include <cstddef>
#include <execution>
#include <span>
#include <vector>

int main() {
    using lanes = std::simd::vec<float, 4>;

    forge::accel::cpu::context ctx;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);

    std::vector<float> input{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    std::vector<float> output(input.size());
    forge::accel::cpu::device_buffer<float> device{ctx, input.size()};

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::copy_to_device(q, device, std::span<const float>{input})).has_value());

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::submit(q, [&] {
            auto data = device.span();
            for (std::size_t offset = 0; offset < data.size(); offset += lanes::size()) {
                const auto remaining = data.size() - offset;
                const auto count = static_cast<std::simd::simd_size_type>(
                    std::min<std::size_t>(remaining, lanes::size()));
                auto values = std::simd::partial_load<lanes>(data.data() + offset, count);
                values = values * lanes(2.0f) + lanes(1.0f);
                std::simd::partial_store(values, data.data() + offset, count);
            }
        })).has_value());

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::copy_to_host(q, std::span<float>{output}, device)).has_value());

    forge_example::require((output == std::vector<float>{3.0f, 5.0f, 7.0f, 9.0f, 11.0f, 13.0f}));
}

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

#include <cassert>
#include "example_support.hpp"
#include <execution>
#include <span>
#include <vector>

int main() {
    forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
        .thread_count = 2,
        .queue_capacity = 8,
    }};
    auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);

    std::vector<float> input{1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> output(input.size());
    forge::accel::cpu::device_buffer<float> device{ctx, input.size()};
    forge::accel::cpu::event uploaded;
    forge::accel::cpu::event computed;

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::copy_to_device(copy_q, device, std::span<const float>{input})).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::record_event(copy_q, uploaded)).has_value());

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::wait_event(compute_q, uploaded)).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::submit(compute_q, [&] {
            for (auto& value : device.span()) {
                value = value * 3.0f;
            }
        })).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::record_event(compute_q, computed)).has_value());

    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::wait_event(copy_q, computed)).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::cpu::copy_to_host(copy_q, std::span<float>{output}, device)).has_value());

    forge_example::require((output == std::vector<float>{3.0f, 6.0f, 9.0f, 12.0f}));
}

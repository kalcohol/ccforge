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
#include <cassert>
#include <span>
#include <tuple>
#include <vector>

int main() {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
    }};
    auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);

    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(input.size());
    forge::accel::mock::device_buffer<int> device{ctx, input.size()};
    forge::accel::mock::event uploaded;
    forge::accel::mock::event computed;

    assert(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(
            copy_q,
            device,
            std::span<const int>{input})).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::record_event(copy_q, uploaded)).has_value());
    assert(uploaded.ready());
    auto uploaded_snapshot = std::execution::sync_wait(
        forge::accel::mock::query_event(uploaded));
    assert(uploaded_snapshot.has_value());
    assert(std::get<0>(*uploaded_snapshot).completed_generation.value == 1);

    assert(std::execution::sync_wait(
        forge::accel::mock::wait_event(compute_q, uploaded)).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::submit(compute_q, [&] {
            for (auto& value : device.span()) {
                value *= 10;
            }
        })).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::record_event(compute_q, computed)).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::synchronize_event(compute_q, computed)).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::wait_event(copy_q, computed)).has_value());
    assert(std::execution::sync_wait(forge::accel::mock::fence(copy_q)).has_value());
    assert(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(copy_q, std::span<int>{output}, device)).has_value());

    assert((output == std::vector<int>{10, 20, 30, 40}));
}

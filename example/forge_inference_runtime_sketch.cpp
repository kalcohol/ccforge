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
#include <forge/channel.hpp>
#include <forge/resource_context.hpp>
#include <forge/strand.hpp>
#include <execution>
#include <array>
#include <cassert>
#include <memory_resource>
#include <span>
#include <tuple>
#include <vector>

struct request {
    std::array<float, 4> values;
};

int main() {
    std::array<std::byte, 16384> storage{};
    std::pmr::monotonic_buffer_resource arena{
        storage.data(),
        storage.size(),
        std::pmr::new_delete_resource()};

    forge::resource_context runtime{forge::resource_context_options{
        .thread_count = 2,
        .queue_capacity = 16,
        .memory = &arena,
    }};
    forge::accel::context accel{forge::accel::context_options{
        .thread_count = 1,
        .queue_capacity = 8,
        .memory = &arena,
    }};
    auto accel_queue = accel.get_queue();
    forge::bounded_channel<request> incoming{forge::bounded_channel_options{
        .capacity = 4,
        .memory = &arena,
    }};
    forge::strand session_order{runtime.get_scheduler(), forge::strand_options{.memory = &arena}};
    std::pmr::vector<float> scores{&arena};

    bool spawned = runtime.spawn(
        std::execution::schedule(runtime.get_scheduler())
        | std::execution::then([&] {
            while (auto item = std::execution::sync_wait(incoming.async_recv())) {
                auto req = std::get<0>(*item);
                forge::accel::device_buffer<float> device{accel, req.values.size()};
                std::array<float, 4> output{};

                assert(std::execution::sync_wait(
                    forge::accel::copy_to_device(
                        accel_queue,
                        device,
                        std::span<const float>{req.values})).has_value());
                assert(std::execution::sync_wait(
                    forge::accel::submit(accel_queue, [&] {
                        for (auto& value : device.span()) {
                            value *= value;
                        }
                    })).has_value());
                assert(std::execution::sync_wait(
                    forge::accel::copy_to_host(
                        accel_queue,
                        std::span<float>{output},
                        device)).has_value());

                float score = output[0] + output[1] + output[2] + output[3];
                assert(std::execution::sync_wait(
                    std::execution::schedule(session_order.get_scheduler())
                    | std::execution::then([&scores, score] {
                        scores.push_back(score);
                    })).has_value());
            }
        }));
    assert(spawned);

    assert(std::execution::sync_wait(
        incoming.async_send(request{{1.0f, 2.0f, 3.0f, 4.0f}})).has_value());
    assert(std::execution::sync_wait(
        incoming.async_send(request{{2.0f, 3.0f, 4.0f, 5.0f}})).has_value());
    incoming.close();

    runtime.wait();
    accel.wait();
    session_order.wait();

    std::pmr::vector<float> expected{&arena};
    expected.push_back(30.0f);
    expected.push_back(54.0f);
    assert(scores == expected);
}

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

#include <forge/channel.hpp>
#include <forge/resource_context.hpp>
#include <forge/strand.hpp>
#include <execution>
#include <array>
#include "example_support.hpp"
#include <memory_resource>
#include <tuple>
#include <vector>

int main() {
    std::array<std::byte, 1024 * 1024> storage{};
    std::pmr::monotonic_buffer_resource upstream{
        storage.data(),
        storage.size(),
        std::pmr::null_memory_resource()};
    std::pmr::synchronized_pool_resource arena{
        std::pmr::pool_options{},
        &upstream};

    forge::resource_context ctx{forge::resource_context_options{
        // The worker blocks at the channel boundary and synchronously hops to a
        // strand; keep a spare runtime worker for those handoffs.
        .thread_count = 2,
        .queue_capacity = 16,
        .memory = &arena,
    }};
    forge::bounded_channel<int> commands{forge::bounded_channel_options{
        .capacity = 4,
        .memory = &arena,
    }};
    forge::strand serial{ctx.get_scheduler(), forge::strand_options{.memory = &arena}};
    std::vector<int> results;

    bool spawned = ctx.spawn(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([&] noexcept {
            while (auto command = std::execution::sync_wait(commands.async_recv())) {
                int value = std::get<0>(*command);
                (void)std::execution::sync_wait(
                    std::execution::schedule(serial.get_scheduler())
                    | std::execution::then([&results, value] noexcept {
                        results.push_back(value * 2);
                    }));
            }
        }));
    forge_example::require(spawned);

    for (int value : {1, 2, 3, 4}) {
        forge_example::require(std::execution::sync_wait(commands.async_send(value)).has_value());
    }
    commands.close();

    ctx.wait();
    serial.wait();

    forge_example::require((results == std::vector<int>{2, 4, 6, 8}));
}

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
#include <forge/task.hpp>
#include <execution>
#include <array>
#include "example_support.hpp"
#include <memory_resource>
#include <latch>
#include <utility>
#include <tuple>
#include <vector>

forge::task<void> consume(
    forge::bounded_channel<int>& commands,
    forge::strand& serial,
    std::vector<int>& results) {
    for (;;) {
        int value = co_await commands.async_recv();
        co_await std::execution::schedule(serial.get_scheduler());
        results.push_back(value * 2);
    }
}

// This receiver only signals completion; the main thread retains the operation
// until the worker has returned from the coroutine's final suspend.
struct consumer_receiver {
    using receiver_concept = std::execution::receiver_tag;
    std::latch* done;
    bool* failed;
    auto get_env() const noexcept { return std::execution::env<>{}; }
    void set_value() && noexcept { done->count_down(); }
    void set_error(std::exception_ptr) && noexcept {
        *failed = true;
        done->count_down();
    }
    void set_stopped() && noexcept { done->count_down(); }
};

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
        // Suspension releases the only worker for subsequent handoffs.
        .thread_count = 1,
        .queue_capacity = 16,
        .memory = &arena,
    }};
    forge::bounded_channel<int> commands{forge::bounded_channel_options{
        .capacity = 4,
        .memory = &arena,
    }};
    forge::strand serial{ctx.get_scheduler(), forge::strand_options{.memory = &arena}};
    std::vector<int> results;

    std::latch consumer_done{1};
    bool consumer_failed = false;
    auto consumer_op = std::execution::connect(
        std::execution::starts_on(
            ctx.get_scheduler(), consume(commands, serial, results)),
        consumer_receiver{&consumer_done, &consumer_failed});
    std::execution::start(consumer_op);

    for (int value : {1, 2, 3, 4}) {
        forge_example::require(std::this_thread::sync_wait(commands.async_send(value)).has_value());
    }
    commands.close();

    consumer_done.wait();
    serial.wait();
    ctx.wait(); // Drain the resume stack before consumer_op can be destroyed.
    forge_example::require(!consumer_failed);

    forge_example::require((results == std::vector<int>{2, 4, 6, 8}));
}

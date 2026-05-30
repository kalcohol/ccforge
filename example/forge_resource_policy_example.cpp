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
#include <forge/static_thread_pool.hpp>
#include <execution>
#include <array>
#include <cassert>
#include <memory_resource>
#include <tuple>

int main() {
    std::array<std::byte, 4096> storage{};
    std::pmr::monotonic_buffer_resource arena{
        storage.data(),
        storage.size(),
        std::pmr::null_memory_resource()};

    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1,
        .queue_capacity = 8,
        .memory = &arena,
    }};
    forge::bounded_channel<int> channel{forge::bounded_channel_options{
        .capacity = 2,
        .memory = &arena,
    }};

    std::execution::start_detached(
        std::execution::schedule(pool.get_scheduler())
        | std::execution::then([&] noexcept {
            auto command = std::execution::sync_wait(channel.async_recv());
            assert(command.has_value());
            assert(std::get<0>(*command) == 21);
        }));

    assert(std::execution::sync_wait(channel.async_send(21)).has_value());
    pool.wait();
}

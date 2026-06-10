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
#include <execution>
#include "example_support.hpp"
#include <tuple>

int main() {
    forge::resource_context ctx{1};
    forge::bounded_channel<int> commands{2};
    forge::bounded_channel<int> events{2};

    bool spawned = ctx.spawn(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([&] noexcept {
            auto command = std::execution::sync_wait(commands.async_recv());
            if (command) {
                (void)std::execution::sync_wait(
                    events.async_send(std::get<0>(*command) + 1));
            }
        }));

    forge_example::require(spawned);
    forge_example::require(std::execution::sync_wait(commands.async_send(6)).has_value());

    auto event = std::execution::sync_wait(events.async_recv());
    forge_example::require(event.has_value());
    forge_example::require(std::get<0>(*event) == 7);

    commands.close();
    ctx.wait();
}


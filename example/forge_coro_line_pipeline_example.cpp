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

#include <forge/io/coro.hpp>
#include <forge/io/memory_stream.hpp>
#include <forge/io/stream.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>

#include "example_support.hpp"

#include <cstddef>
#include <execution>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io;

struct shared_state {
    std::size_t handled = 0;
    std::string last_command;
};

auto to_string(std::span<const std::byte> bytes) -> std::string {
    std::string text;
    text.reserve(bytes.size());
    for (std::byte byte : bytes) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return text;
}

auto parse_command(std::string_view command) -> std::string {
    if (command == "PING") {
        return "PONG\n";
    }
    return "ERR\n";
}

auto handle_request(
    forge::io::any_read_stream& input,
    forge::io::any_write_stream& output,
    forge::static_thread_pool::scheduler cpu,
    forge::strand::scheduler serial,
    shared_state& state) -> cio::io_task<forge::io::io_result<std::size_t>> {
    std::string command;
    auto [read_error, read_count] =
        forge::io::read_until(input, command, '\n', 32);
    if (read_error) {
        co_return forge::io::io_result<std::size_t>::failure(
            read_error,
            read_count);
    }
    if (!command.empty() && command.back() == '\n') {
        command.pop_back();
    }

    co_await cio::await_sender(std::execution::schedule(cpu));
    auto response = parse_command(command);

    co_await cio::await_sender(std::execution::schedule(serial));
    ++state.handled;
    state.last_command = command;

    auto [write_error, written] = forge::io::write_all(
        output,
        forge::io::const_buffer{response.data(), response.size()});
    if (write_error) {
        co_return forge::io::io_result<std::size_t>::failure(
            write_error,
            written);
    }

    co_return forge::io::io_result<std::size_t>::success(written);
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::io::scripted_read_stream scripted{
        forge::io::scripted_read_step::bytes("PI"),
        forge::io::scripted_read_step::bytes("NG\n")};
    forge::io::any_read_stream input{scripted};

    forge::io::memory_write_stream memory_output;
    forge::io::any_write_stream output{memory_output};

    forge::static_thread_pool pool{1};
    forge::strand serial{pool.get_scheduler()};
    shared_state state;

    auto result = std::execution::sync_wait(
        cio::as_sender(handle_request(
            input,
            output,
            pool.get_scheduler(),
            serial.get_scheduler(),
            state)));
    forge_example::require(result.has_value());

    auto [io] = std::move(*result);
    auto [error, written] = io;
    forge_example::require(!error);
    forge_example::require(written == 5u);
    forge_example::require(state.handled == 1u);
    forge_example::require(state.last_command == "PING");
    forge_example::require(to_string(memory_output.bytes()) == "PONG\n");

    serial.wait();
    pool.wait();

    std::cout << "coro line pipeline response: "
              << to_string(memory_output.bytes());
#else
    std::cout << "coro line pipeline unavailable\n";
#endif
}

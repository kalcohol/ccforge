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

#include <forge/io/memory_stream.hpp>
#include <forge/io/stream.hpp>

#include "example_support.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

auto to_string(std::span<const std::byte> bytes) -> std::string {
    std::string text;
    text.reserve(bytes.size());
    for (std::byte byte : bytes) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return text;
}

auto read_command(forge::io::any_read_stream& input)
    -> forge::io::io_result<std::string> {
    std::string line;
    auto result = forge::io::read_until(input, line, '\n', 32);
    auto [error, count] = result;
    if (error) {
        return forge::io::io_result<std::string>::failure(
            error,
            std::move(line));
    }
    if (result.eof()) {
        return forge::io::io_result<std::string>::end_of_file(
            std::move(line));
    }

    if (count != 0 && !line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    return forge::io::io_result<std::string>::success(std::move(line));
}

auto write_response(
    forge::io::any_write_stream& output,
    std::string_view response) -> forge::io::io_result<std::size_t> {
    return forge::io::write_all(
        output,
        forge::io::const_buffer{response.data(), response.size()});
}

} // namespace

int main() {
    forge::io::scripted_read_stream scripted{
        forge::io::scripted_read_step::bytes("PI"),
        forge::io::scripted_read_step::bytes("NG\n")};
    forge::io::any_read_stream input{scripted};

    forge::io::memory_write_stream memory_output;
    forge::io::any_write_stream output{memory_output};

    auto [read_error, command] = read_command(input);
    forge_example::require(!read_error);
    forge_example::require(command == "PING");

    auto response = command == "PING"
        ? std::string_view{"PONG\n"}
        : std::string_view{"ERR\n"};
    auto [write_error, written] = write_response(output, response);
    forge_example::require(!write_error);
    forge_example::require(written == response.size());
    forge_example::require(to_string(memory_output.bytes()) == "PONG\n");

    std::cout << "line protocol response: "
              << to_string(memory_output.bytes());
}

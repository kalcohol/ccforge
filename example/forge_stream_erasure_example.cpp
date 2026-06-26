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

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

auto to_bytes(std::string_view text) -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (char ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

auto read_packet(forge::io::any_read_stream& stream)
    -> forge::io::io_result<std::string> {
    std::array<std::byte, 1> length_storage{};
    auto length_result = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{std::span{length_storage}});
    auto [length_error, length_count] = length_result;
    if (length_error) {
        return forge::io::io_result<std::string>::failure(
            length_error,
            std::string{});
    }
    if (length_result.eof()) {
        return forge::io::io_result<std::string>::end_of_file(std::string{});
    }
    if (length_count != 1) {
        return forge::io::io_result<std::string>::failure(
            std::make_error_code(std::errc::io_error),
            std::string{});
    }

    const auto expected =
        static_cast<std::size_t>(std::to_integer<unsigned char>(
            length_storage[0]));
    std::string payload(expected, '\0');
    auto payload_result = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{payload.data(), payload.size()});
    auto [payload_error, payload_count] = payload_result;
    if (payload_error) {
        payload.resize(payload_count);
        return forge::io::io_result<std::string>::failure(
            payload_error,
            std::move(payload));
    }
    if (payload_result.eof()) {
        payload.resize(payload_count);
        return forge::io::io_result<std::string>::end_of_file(
            std::move(payload));
    }

    return forge::io::io_result<std::string>::success(std::move(payload));
}

} // namespace

int main() {
    auto prefix = to_bytes("he");
    prefix.insert(prefix.begin(), std::byte{5});
    forge::io::scripted_read_stream scripted{
        forge::io::scripted_read_step::bytes(
            forge::io::const_buffer{prefix.data(), prefix.size()}),
        forge::io::scripted_read_step::bytes("llo")};
    forge::io::any_read_stream input{scripted};

    auto [read_error, payload] = read_packet(input);
    forge_example::require(!read_error);
    forge_example::require(payload == "hello");

    forge::io::memory_write_stream memory_output;
    forge::io::any_write_stream output{memory_output};
    auto [write_error, written] = forge::io::write_all(
        output,
        forge::io::const_buffer{payload.data(), payload.size()});
    forge_example::require(!write_error);
    forge_example::require(written == payload.size());

    std::cout << "stream erasure packet: " << payload << '\n';
}

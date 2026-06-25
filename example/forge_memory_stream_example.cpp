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

template<class Stream>
auto read_length_prefixed_packet(Stream& stream)
    -> forge::io::io_result<std::string> {
    std::array<std::byte, 1> length_storage{};
    auto [length_error, length_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{length_storage}});
    if (length_error || length_count != 1) {
        return forge::io::io_result<std::string>::failure(
            length_error ? length_error : std::make_error_code(std::errc::io_error),
            std::string{});
    }

    const auto expected =
        static_cast<std::size_t>(std::to_integer<unsigned char>(
            length_storage[0]));
    std::string payload(expected, '\0');
    std::size_t offset = 0;
    while (offset < expected) {
        auto [read_error, read_count] = stream.read_some(
            forge::io::mutable_buffer{
                payload.data() + offset,
                payload.size() - offset});
        offset += read_count;
        if (read_error || read_count == 0) {
            payload.resize(offset);
            return forge::io::io_result<std::string>::failure(
                read_error ? read_error : std::make_error_code(std::errc::io_error),
                std::move(payload));
        }
    }

    return forge::io::io_result<std::string>::success(std::move(payload));
}

} // namespace

int main() {
    auto first = to_bytes("he");
    first.insert(first.begin(), std::byte{5});
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes(
            forge::io::const_buffer{first.data(), first.size()}),
        forge::io::scripted_read_step::bytes("llo"),
        forge::io::scripted_read_step::eof()};

    auto [read_error, payload] = read_length_prefixed_packet(stream);
    forge_example::require(!read_error);
    forge_example::require(payload == "hello");

    forge::io::memory_write_stream output;
    auto [write_error, written] = output.write_some(
        forge::io::const_buffer{payload.data(), payload.size()});
    forge_example::require(!write_error);
    forge_example::require(written == payload.size());

    std::cout << "memory stream packet: " << payload << '\n';
}

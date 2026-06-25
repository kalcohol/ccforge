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

#include <forge/io/buffer.hpp>
#include <forge/io/result.hpp>

#include "example_support.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

auto fake_read_some(forge::io::mutable_buffer out)
    -> forge::io::io_result<std::size_t> {
    constexpr std::string_view payload = "forge";
    const auto copied = forge::io::buffer_copy(
        out,
        forge::io::const_buffer{payload.data(), payload.size()});
    return forge::io::io_result<std::size_t>::success(copied);
}

int main() {
    std::array<char, 16> storage{};

    auto [ec, n] = fake_read_some(
        forge::io::mutable_buffer{std::span{storage}});
    forge_example::require(!ec);
    forge_example::require(n == 5);

    std::cout << "byte vocabulary read: "
              << std::string_view(storage.data(), n) << '\n';
}

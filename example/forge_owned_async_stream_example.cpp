// MIT License
//
// Copyright (c) 2026 CC Forge Project
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

#include "forge_owned_async_stream_protocol.hpp"

#include <forge/io/memory_stream.hpp>

#include <execution>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

int main() {
    std::string input{"owned async boundary"};
    forge::io::owning_any_async_read_stream stream{
        forge::io::immediate_async_stream{
            forge::io::memory_read_stream{
                std::string_view{input},
                3}}};

    auto completion = std::this_thread::sync_wait(
        forge::io::as_sender(
            forge_example::read_owned_async_message(stream)));
    if (!completion.has_value()) {
        std::cerr << "operation stopped\n";
        return 1;
    }

    auto [result] = std::move(*completion);
    const auto reached_eof = result.eof();
    auto [error, message] = std::move(result);
    if (error) {
        std::cerr << error.message() << '\n';
        return 2;
    }
    if (!reached_eof || message != input) {
        std::cerr << "unexpected message\n";
        return 3;
    }

    std::cout << message << '\n';
    return 0;
}

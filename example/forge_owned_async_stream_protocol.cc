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

#include <array>
#include <cstddef>
#include <span>
#include <system_error>
#include <utility>

namespace forge_example {

auto read_owned_async_message(
    forge::io::owning_any_async_read_stream& stream)
    -> forge::io::io_task<forge::io::io_result<std::string>> {
    std::string message;
    std::array<char, 4> chunk{};

    for (;;) {
        auto result = co_await stream.read_some(
            forge::io::mutable_buffer{std::span{chunk}});
        auto [error, count] = result;
        message.append(chunk.data(), count);

        if (error) {
            co_return forge::io::io_result<std::string>::failure(
                error,
                std::move(message));
        }
        if (result.eof()) {
            co_return forge::io::io_result<std::string>::end_of_file(
                std::move(message));
        }
        if (count == 0) {
            co_return forge::io::io_result<std::string>::failure(
                std::make_error_code(std::errc::io_error),
                std::move(message));
        }
    }
}

} // namespace forge_example

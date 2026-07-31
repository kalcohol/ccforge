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

#include <forge/io/combinators.hpp>

#include "example_support.hpp"

#include <cstddef>
#include <execution>
#include <iostream>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io;

using byte_count = cio::io_result<std::size_t>;

auto bytes_read(std::size_t count) -> cio::io_task<byte_count> {
    co_return byte_count::success(count);
}

auto eof_after(std::size_t count) -> cio::io_task<byte_count> {
    co_return byte_count::end_of_file(count);
}

auto write_error_after(std::size_t count) -> cio::io_task<byte_count> {
    co_return byte_count::failure(
        std::make_error_code(std::errc::broken_pipe),
        count);
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    auto eof_join = std::this_thread::sync_wait(cio::when_all_results(
        bytes_read(12),
        eof_after(4)));
    forge_example::require(eof_join.has_value());

    auto [eof_result] = std::move(*eof_join);
    forge_example::require(eof_result.eof());
    auto [eof_error, eof_payload] = eof_result;
    forge_example::require(!eof_error);
    forge_example::require(eof_payload.first.has_value());
    forge_example::require(eof_payload.second.has_value());
    forge_example::require(cio::get<1>(*eof_payload.first) == 12u);
    forge_example::require(cio::get<1>(*eof_payload.second) == 4u);

    auto error_join = std::this_thread::sync_wait(cio::when_all_results(
        bytes_read(3),
        write_error_after(2)));
    forge_example::require(error_join.has_value());

    auto [error_result] = std::move(*error_join);
    forge_example::require(error_result.status() == cio::io_status::error);
    auto [error, error_payload] = error_result;
    forge_example::require(error == std::make_error_code(std::errc::broken_pipe));
    forge_example::require(error_payload.first.has_value());
    forge_example::require(error_payload.second.has_value());
    forge_example::require(cio::get<1>(*error_payload.first) == 3u);
    forge_example::require(cio::get<1>(*error_payload.second) == 2u);

    std::cout << "coro combinator kept partial results across EOF and error\n";
#else
    std::cout << "coro combinator unavailable\n";
#endif
}

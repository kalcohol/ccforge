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

#include <forge/erased_sender.hpp>
#include <forge/io/coro.hpp>

#include "example_support.hpp"

#include <cstddef>
#include <exception>
#include <execution>
#include <iostream>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace cio = forge::io::experimental;

auto parse_frame_size(std::byte length_byte) -> cio::io_task<std::size_t> {
    auto [bias] = co_await cio::await_sender(std::execution::just(0u));
    co_return std::to_integer<unsigned char>(length_byte) + bias;
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(std::size_t),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    forge::erased_sender<completions> operation{
        cio::as_sender(parse_frame_size(std::byte{5}))};
    auto result = std::execution::sync_wait(std::move(operation));
    forge_example::require(result.has_value());
    forge_example::require(std::get<0>(*result) == 5u);

    std::cout << "coro interop frame size: " << std::get<0>(*result) << '\n';
#else
    std::cout << "coro interop unavailable\n";
#endif
}

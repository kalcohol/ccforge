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

#pragma once

#include <forge/io/coro.hpp>
#include <forge/io/error.hpp>
#include <forge/io/result.hpp>

#include <cstddef>
#include <exception>
#include <span>
#include <system_error>
#include <utility>

#if defined(FORGE_HAS_FORGE_IO_BACKEND)
#include <forge/io/context.hpp>
#endif

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace __context_await_detail {

[[nodiscard]] inline auto exception_code(std::exception_ptr exception)
    -> std::error_code {
    auto typed = forge::io::typed_detail::from_exception(std::move(exception));
    if (typed.code) {
        return typed.code;
    }
    return std::make_error_code(std::errc::io_error);
}

template<class Sender>
auto await_size_result(Sender sender) -> io_task<io_result<std::size_t>> {
    try {
        auto [count] = co_await await_sender(std::move(sender));
        co_return io_result<std::size_t>::success(count);
    } catch (const sender_stopped&) {
        throw;
    } catch (...) {
        co_return io_result<std::size_t>::failure(
            exception_code(std::current_exception()),
            0);
    }
}

template<class Sender>
auto await_void_result(Sender sender) -> io_task<io_result<>> {
    try {
        co_await await_sender(std::move(sender));
        co_return io_result<>::success();
    } catch (const sender_stopped&) {
        throw;
    } catch (...) {
        co_return io_result<>::failure(exception_code(std::current_exception()));
    }
}

} // namespace __context_await_detail

#if defined(FORGE_HAS_FORGE_IO_BACKEND)

template<class Handle>
[[nodiscard]] auto async_read_some(
    forge::io::context& context,
    Handle handle,
    std::span<std::byte> buffer) -> io_task<io_result<std::size_t>> {
    return __context_await_detail::await_size_result(
        context.async_read_some(handle, buffer));
}

template<class Handle>
[[nodiscard]] auto async_write_some(
    forge::io::context& context,
    Handle handle,
    std::span<const std::byte> buffer) -> io_task<io_result<std::size_t>> {
    return __context_await_detail::await_size_result(
        context.async_write_some(handle, buffer));
}

#if defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)

[[nodiscard]] inline auto readable(forge::io::context& context, int fd)
    -> io_task<io_result<>> {
    return __context_await_detail::await_void_result(context.readable(fd));
}

[[nodiscard]] inline auto writable(forge::io::context& context, int fd)
    -> io_task<io_result<>> {
    return __context_await_detail::await_void_result(context.writable(fd));
}

#endif // FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND

#endif // FORGE_HAS_FORGE_IO_BACKEND

#endif // __cpp_impl_coroutine

} // namespace forge::io

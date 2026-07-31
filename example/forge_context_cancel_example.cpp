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

#include <forge/io/context_await.hpp>

#include "example_support.hpp"

#include <exception>
#include <execution>
#include <iostream>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)

namespace cio = forge::io;
using namespace std::chrono_literals;

class unique_fd {
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept : fd_(fd) {}
    ~unique_fd() noexcept { reset(); }

    unique_fd(unique_fd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1))
    {}

    auto operator=(unique_fd&& other) noexcept -> unique_fd& {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    unique_fd(const unique_fd&) = delete;
    auto operator=(const unique_fd&) -> unique_fd& = delete;

    [[nodiscard]] auto get() const noexcept -> int { return fd_; }

    auto reset(int next = -1) noexcept -> void {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

auto wait_for_readiness(
    forge::io::context& context,
    int fd) -> cio::io_task<bool> {
    auto [error] = co_await cio::readable(context, fd);
    co_return !error;
}

#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
    int fds[2]{-1, -1};
    forge_example::require(::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
    unique_fd read_fd{fds[0]};
    unique_fd write_fd{fds[1]};

    forge::io::context context;
    std::inplace_stop_source stop;
    cio::io_env env;
    env.stop_token = stop.get_token();

    std::optional<std::tuple<bool>> result;
    std::exception_ptr error;
    std::thread waiter([&] {
        try {
            result = std::this_thread::sync_wait(cio::as_sender(
                wait_for_readiness(context, read_fd.get()),
                env));
        } catch (...) {
            error = std::current_exception();
        }
    });

    std::this_thread::sleep_for(20ms);
    stop.request_stop();
    waiter.join();

    if (error) {
        std::rethrow_exception(error);
    }
    forge_example::require(!result.has_value());

    std::cout << "context await cancellation observed\n";
#else
    std::cout << "context await cancellation unavailable\n";
#endif
}

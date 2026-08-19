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

#include <forge/io/io_uring_context.hpp>

#include "example_support.hpp"

#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstddef>
#include <execution>
#include <optional>
#include <span>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

namespace cio = forge::io;

namespace {

class unique_fd {
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept : fd_(fd) {}
    ~unique_fd() noexcept { reset(); }

    unique_fd(unique_fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(unique_fd&& other) noexcept -> unique_fd& {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    unique_fd(const unique_fd&) = delete;
    auto operator=(const unique_fd&) -> unique_fd& = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

    void reset(int next = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

// One coroutine composes both native awaits; the CQE resumes it directly on
// the context poller thread.
auto echo(
    cio::io_uring_context& context,
    int write_fd,
    int read_fd,
    std::span<const std::byte> outbound,
    std::span<std::byte> inbound) -> cio::io_task<cio::io_result<std::size_t>> {
    auto wrote = co_await cio::async_write_some(context, write_fd, outbound);
    if (!wrote.has_value()) {
        co_return wrote;
    }
    co_return co_await cio::async_read_some(context, read_fd, inbound);
}

} // namespace

int main() {
    int fds[2]{-1, -1};
    forge_example::require(::pipe(fds) == 0);
    unique_fd read_fd{fds[0]};
    unique_fd write_fd{fds[1]};

    // Restricted sandboxes commonly deny the io_uring syscalls; the example
    // then reports the condition instead of failing.
    std::optional<cio::io_uring_context> context;
    try {
        context.emplace();
    } catch (const std::system_error& error) {
        std::printf(
            "io_uring backend unavailable here: %s\n",
            error.what());
        return 0;
    }

    std::array<char, 5> text{'h', 'e', 'l', 'l', 'o'};
    std::array<std::byte, 16> inbound{};

    auto result = std::this_thread::sync_wait(cio::as_sender(echo(
        *context,
        write_fd.get(),
        read_fd.get(),
        std::as_bytes(std::span{text}),
        inbound)));
    forge_example::require(result.has_value());

    auto [io] = std::move(*result);
    forge_example::require(io.has_value());
    forge_example::require(std::get<0>(io.values()) == text.size());

    auto expected = std::as_bytes(std::span{text});
    for (std::size_t i = 0; i < text.size(); ++i) {
        forge_example::require(inbound[i] == expected[i]);
    }

    std::printf(
        "io_uring echo round trip moved %zu bytes\n",
        std::get<0>(io.values()));
    return 0;
}

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

#include <forge/io.hpp>

#include <execution>
#include <array>
#include "example_support.hpp"
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <tuple>
#include <unistd.h>
#include <utility>

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

int main() {
    int fds[2]{-1, -1};
    forge_example::require(::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
    unique_fd read_fd{fds[0]};
    unique_fd write_fd{fds[1]};

    forge::io::context io;

    std::array<char, 5> outbound{'h', 'e', 'l', 'l', 'o'};
    auto wrote = std::this_thread::sync_wait(
        io.async_write_some(write_fd.get(), std::as_bytes(std::span{outbound})));
    forge_example::require(wrote.has_value());
    forge_example::require(std::get<0>(*wrote) == outbound.size());

    std::array<std::byte, 5> inbound{};
    auto read = std::this_thread::sync_wait(
        io.async_read_some(read_fd.get(), std::span{inbound}));
    forge_example::require(read.has_value());
    forge_example::require(std::get<0>(*read) == inbound.size());

    auto text = std::as_bytes(std::span{outbound});
    for (std::size_t i = 0; i < inbound.size(); ++i) {
        forge_example::require(inbound[i] == text[i]);
    }
}

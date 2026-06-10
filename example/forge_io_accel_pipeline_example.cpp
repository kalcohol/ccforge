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

#include <forge/accel.hpp>
#include <forge/io.hpp>

#include <array>
#include "example_support.hpp"
#include <cstddef>
#include <execution>
#include <fcntl.h>
#include <span>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

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
    forge::accel::cpu::context accel{forge::accel::cpu::context_options{
        .thread_count = 2,
        .queue_capacity = 8,
    }};
    auto copy_q = accel.get_queue(forge::accel::queue_kind::copy);
    auto compute_q = accel.get_queue(forge::accel::queue_kind::compute);

    std::vector<float> outbound{1.0f, 2.0f, 3.0f, 4.0f};
    auto wrote = std::execution::sync_wait(
        io.async_write_some(write_fd.get(), std::as_bytes(std::span{outbound})));
    forge_example::require(wrote.has_value());
    forge_example::require(std::get<0>(*wrote) == outbound.size() * sizeof(float));

    std::vector<float> inbound(outbound.size());
    std::vector<float> output(outbound.size());
    forge::accel::cpu::device_buffer<float> device{accel, outbound.size()};

    auto pipeline =
        io.async_read_some(
            read_fd.get(),
            std::as_writable_bytes(std::span{inbound}))
        | std::execution::let_value([&](std::size_t bytes) {
              forge_example::require(bytes == inbound.size() * sizeof(float));
              return forge::accel::cpu::copy_to_device(
                  copy_q,
                  device,
                  std::span<const float>{inbound});
          })
        | std::execution::let_value([&] {
              return forge::accel::cpu::submit(compute_q, [&] {
                  for (auto& value : device.span()) {
                      value = value * 2.0f + 1.0f;
                  }
              });
          })
        | std::execution::let_value([&] {
              return forge::accel::cpu::copy_to_host(
                  copy_q,
                  std::span<float>{output},
                  device);
          });

    forge_example::require(std::execution::sync_wait(std::move(pipeline)).has_value());
    forge_example::require((output == std::vector<float>{3.0f, 5.0f, 7.0f, 9.0f}));
}

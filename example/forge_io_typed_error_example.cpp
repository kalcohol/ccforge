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
#include <forge/io.hpp>

#include <execution>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

using namespace std::chrono_literals;

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

    [[nodiscard]] auto get() const noexcept -> int { return fd_; }

    void reset(int next = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

struct pipe_pair {
    unique_fd read;
    unique_fd write;
};

[[nodiscard]] auto make_pipe() -> pipe_pair {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        throw std::system_error{errno, std::generic_category(), "pipe2"};
    }
    return pipe_pair{unique_fd{fds[0]}, unique_fd{fds[1]}};
}

struct state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    bool error = false;
    forge::io::error io_error{};

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<state> out;

    void set_value() && noexcept {
        {
            std::lock_guard lk{out->mtx};
            out->value = true;
        }
        out->cv.notify_all();
    }

    void set_error(forge::io::error error) && noexcept {
        {
            std::lock_guard lk{out->mtx};
            out->error = true;
            out->io_error = error;
        }
        out->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{out->mtx};
            out->stopped = true;
        }
        out->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

void wait_done(const std::shared_ptr<state>& out) {
    std::unique_lock lk{out->mtx};
    const bool done = out->cv.wait_for(lk, 2s, [&] { return out->done(); });
    assert(done);
}

using readiness_operation = forge::erased_sender<
    std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::io::error),
        std::execution::set_stopped_t()>>;

} // namespace

int main() {
    auto pipe = make_pipe();
    forge::io::context io;

    auto first_state = std::make_shared<state>();
    auto second_state = std::make_shared<state>();

    auto first = std::execution::connect(
        io.readable_typed(pipe.read.get()),
        receiver{first_state});
    std::execution::start(first);

    readiness_operation second_sender{io.readable_typed(pipe.read.get())};
    auto second = std::execution::connect(
        std::move(second_sender),
        receiver{second_state});
    std::execution::start(second);

    wait_done(second_state);
    assert(second_state->error);
    assert(second_state->io_error.kind == forge::io::error_kind::operation_in_progress);

    io.cancel(pipe.read.get());
    wait_done(first_state);
    assert(first_state->stopped);
}

#pragma once

#if defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)

#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace forge_test {

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

    void reset(int next = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

struct fd_pair {
    unique_fd first;
    unique_fd second;
};

inline auto make_pipe() -> fd_pair {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        throw std::runtime_error{"pipe2 failed"};
    }
    return fd_pair{unique_fd{fds[0]}, unique_fd{fds[1]}};
}

inline auto make_socketpair() -> fd_pair {
    int fds[2]{-1, -1};
    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            0,
            fds) != 0) {
        throw std::runtime_error{"socketpair failed"};
    }
    return fd_pair{unique_fd{fds[0]}, unique_fd{fds[1]}};
}

} // namespace forge_test

#endif

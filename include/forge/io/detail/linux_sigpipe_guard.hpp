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

#include <cerrno>
#include <system_error>

#include <pthread.h>
#include <signal.h>
#include <time.h>

namespace forge::io::__signal_detail {

// Blocks SIGPIPE on the calling thread for the duration of a write-capable
// syscall so a peer-closed pipe surfaces as EPIPE instead of killing the
// process. A signal generated while blocked stays thread-pending and is
// consumed with a zero-timeout sigtimedwait; a SIGPIPE that was already
// pending before the guard is preserved for its original consumer.
class sigpipe_guard {
public:
    sigpipe_guard() {
        ::sigemptyset(&mask_);
        ::sigaddset(&mask_, SIGPIPE);
        const int mask_error = ::pthread_sigmask(SIG_BLOCK, &mask_, &old_mask_);
        if (mask_error != 0) {
            throw std::system_error{
                mask_error,
                std::generic_category(),
                "forge::io block SIGPIPE"};
        }
        active_ = true;

        sigset_t pending{};
        if (::sigpending(&pending) != 0) {
            const int error = errno;
            restore();
            throw std::system_error{
                error,
                std::generic_category(),
                "forge::io inspect SIGPIPE"};
        }
        was_pending_ = ::sigismember(&pending, SIGPIPE) == 1;
    }

    ~sigpipe_guard() { restore(); }

    sigpipe_guard(const sigpipe_guard&) = delete;
    auto operator=(const sigpipe_guard&) -> sigpipe_guard& = delete;

    void consume_generated_signal() noexcept {
        if (was_pending_) {
            return;
        }
        const timespec timeout{};
        while (::sigtimedwait(&mask_, nullptr, &timeout) < 0 && errno == EINTR) {}
    }

private:
    void restore() noexcept {
        if (active_) {
            (void)::pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
            active_ = false;
        }
    }

    sigset_t mask_{};
    sigset_t old_mask_{};
    bool active_ = false;
    bool was_pending_ = false;
};

} // namespace forge::io::__signal_detail

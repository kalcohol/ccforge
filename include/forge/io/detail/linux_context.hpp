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

#if !defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
#error "forge::io linux context requires FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND"
#endif

#include "../error.hpp"
#include "../../resource_policy.hpp"

#include <execution>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <signal.h>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <time.h>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/epoll.h>
#include <sys/eventfd.h>

namespace forge::io {

enum class readiness {
    read,
    write
};

struct context_options {
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    std::size_t max_events = 64;
};

class context;

namespace __detail {

template<class R>
bool __stop_requested(const R& rcvr) noexcept {
    if constexpr (requires {
                      std::execution::get_stop_token(
                          std::execution::get_env(rcvr));
                  }) {
        return std::execution::get_stop_token(
            std::execution::get_env(rcvr)).stop_requested();
    } else {
        return false;
    }
}

[[nodiscard]] inline auto __system_error(std::errc code, const char* what)
    -> std::exception_ptr {
    return std::make_exception_ptr(std::system_error{
        std::make_error_code(code), what});
}

[[nodiscard]] inline auto __system_error(int error_number, const char* what)
    -> std::exception_ptr {
    return std::make_exception_ptr(std::system_error{
        error_number, std::generic_category(), what});
}

[[nodiscard]] inline auto __read_some(int fd, std::span<std::byte> buffer)
    -> std::size_t {
    while (true) {
        const auto result = ::read(fd, buffer.data(), buffer.size());
        if (result >= 0) {
            return static_cast<std::size_t>(result);
        }
        if (errno == EINTR) {
            continue;
        }
        throw std::system_error{errno, std::generic_category(),
                                "forge::io async_read_some"};
    }
}

class __sigpipe_guard {
public:
    __sigpipe_guard() {
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

    ~__sigpipe_guard() { restore(); }

    __sigpipe_guard(const __sigpipe_guard&) = delete;
    auto operator=(const __sigpipe_guard&) -> __sigpipe_guard& = delete;

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

[[nodiscard]] inline auto __write_some(int fd, std::span<const std::byte> buffer)
    -> std::size_t {
    while (true) {
        __sigpipe_guard guard;
        const auto result = ::write(fd, buffer.data(), buffer.size());
        if (result >= 0) {
            return static_cast<std::size_t>(result);
        }
        const int error = errno;
        if (error == EPIPE) {
            guard.consume_generated_signal();
        }
        if (error == EINTR) {
            continue;
        }
        throw std::system_error{error, std::generic_category(),
                                "forge::io async_write_some"};
    }
}

class __fd {
public:
    __fd() noexcept = default;
    explicit __fd(int fd) noexcept : fd_(fd) {}

    ~__fd() noexcept {
        reset();
    }

    __fd(__fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(__fd&& other) noexcept -> __fd& {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    __fd(const __fd&) = delete;
    auto operator=(const __fd&) -> __fd& = delete;

    [[nodiscard]] auto get() const noexcept -> int { return fd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

    [[nodiscard]] auto release() noexcept -> int {
        return std::exchange(fd_, -1);
    }

    void reset(int next = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

enum class __completion_kind {
    value,
    error,
    stopped
};

struct __record_base {
    virtual ~__record_base() = default;
    virtual void complete_value() noexcept = 0;
    virtual void complete_error(std::exception_ptr error) noexcept = 0;
    virtual void complete_stopped() noexcept = 0;

    int fd = -1;
    readiness kind = readiness::read;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> done{false};
    std::shared_ptr<__record_base> next_action;
    std::exception_ptr action_error;
    __completion_kind action_kind = __completion_kind::value;
};

struct __state;

struct __stop_callback_fn {
    std::weak_ptr<__state> state;
    std::weak_ptr<__record_base> record;

    void operator()() const noexcept;
};

template<class R>
struct __record final : __record_base {
    explicit __record(int file, readiness ready, R r)
        : rcvr(std::move(r)) {
        fd = file;
        kind = ready;
    }

    void complete_value() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_value(std::move(rcvr));
    }

    void complete_error(std::exception_ptr error) noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_error(std::move(rcvr), std::move(error));
    }

    void complete_stopped() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_stopped(std::move(rcvr));
    }

    using callback_t =
        std::stop_callback_for_t<std::any_stop_token, __stop_callback_fn>;

    [[nodiscard]] bool install_stop_callback(
        std::weak_ptr<__state> state,
        std::weak_ptr<__record_base> self) noexcept;

    R rcvr;
    std::optional<callback_t> stop_callback;
};

using __record_ptr = std::shared_ptr<__record_base>;

struct __actions {
    void value(__record_ptr record) noexcept {
        push(std::move(record), __completion_kind::value, {});
    }

    void error(
        __record_ptr record,
        std::exception_ptr error) noexcept {
        push(
            std::move(record),
            __completion_kind::error,
            std::move(error));
    }

    void stopped(__record_ptr record) noexcept {
        push(std::move(record), __completion_kind::stopped, {});
    }

    void run() noexcept {
        while (head) {
            auto record = std::move(head);
            head = std::move(record->next_action);
            if (!head) {
                tail = nullptr;
            }
            auto error = std::move(record->action_error);
            const auto kind = record->action_kind;
            switch (kind) {
            case __completion_kind::value:
                record->complete_value();
                break;
            case __completion_kind::error:
                record->complete_error(std::move(error));
                break;
            case __completion_kind::stopped:
                record->complete_stopped();
                break;
            }
        }
    }

private:
    void push(
        __record_ptr record,
        __completion_kind kind,
        std::exception_ptr error) noexcept {
        record->action_kind = kind;
        record->action_error = std::move(error);
        record->next_action.reset();
        auto* next_tail = record.get();
        if (tail) {
            tail->next_action = std::move(record);
        } else {
            head = std::move(record);
        }
        tail = next_tail;
    }

    __record_ptr head;
    __record_base* tail = nullptr;
};

struct __fd_waiters {
    __record_ptr read;
    __record_ptr write;
    bool registered = false;

    [[nodiscard]] auto empty() const noexcept -> bool {
        return !read && !write;
    }

    [[nodiscard]] auto events() const noexcept -> std::uint32_t {
        std::uint32_t result = EPOLLERR | EPOLLHUP;
        if (read) {
            result |= EPOLLIN | EPOLLRDHUP;
        }
        if (write) {
            result |= EPOLLOUT;
        }
        return result;
    }
};

struct __state : std::enable_shared_from_this<__state> {
    explicit __state(context_options options)
        : memory(forge::normalize_memory_resource(options.memory))
        , fd_waiters(
              0,
              std::hash<int>{},
              std::equal_to<int>{},
              std::pmr::polymorphic_allocator<std::pair<const int, __fd_waiters>>{
                  memory})
        , events(memory) {
        const auto event_count = options.max_events == 0 ? 1 : options.max_events;
        events.resize(event_count);

        epoll.reset(::epoll_create1(EPOLL_CLOEXEC));
        if (!epoll) {
            throw std::system_error{errno, std::generic_category(),
                                    "epoll_create1"};
        }

        wake.reset(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
        if (!wake) {
            throw std::system_error{errno, std::generic_category(), "eventfd"};
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = wake.get();
        if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, wake.get(), &event) != 0) {
            throw std::system_error{errno, std::generic_category(),
                                    "epoll_ctl eventfd add"};
        }
    }

    [[nodiscard]] auto start(__record_ptr record) -> bool {
        __actions actions;
        bool should_wake = false;
        bool became_pending = false;
        {
            std::lock_guard lk{mtx};
            if (closed || stopped) {
                actions.stopped(std::move(record));
            } else if (record->fd < 0) {
                actions.error(std::move(record),
                    __system_error(std::errc::bad_file_descriptor,
                                   "forge::io readiness invalid fd"));
            } else {
                auto [it, inserted] = fd_waiters.try_emplace(record->fd);
                auto& waiters = it->second;
                auto& slot = record->kind == readiness::read
                    ? waiters.read
                    : waiters.write;

                if (slot) {
                    actions.error(std::move(record),
                        __system_error(std::errc::operation_in_progress,
                                       "forge::io duplicate readiness waiter"));
                } else {
                    slot = std::move(record);
                    ++pending;
                    if (!update_interest_locked(it->first, waiters)) {
                        const int update_error = errno;
                        auto failed = record_from_slot(waiters, slot);
                        if (failed) {
                            --pending;
                            actions.error(std::move(failed),
                                __system_error(
                                    update_error,
                                    "epoll_ctl readiness add"));
                        }
                        if (waiters.empty()) {
                            fd_waiters.erase(it);
                        }
                    } else {
                        should_wake = true;
                        became_pending = true;
                    }
                }
            }
        }
        if (should_wake) {
            wake_polling_thread();
        }
        actions.run();
        return became_pending;
    }

    void cancel(int fd) noexcept {
        __actions actions;
        {
            std::lock_guard lk{mtx};
            auto it = fd_waiters.find(fd);
            if (it != fd_waiters.end()) {
                take_all_locked(it, actions, __completion_kind::stopped);
            }
        }
        wake_polling_thread();
        actions.run();
    }

    void cancel_record(const __record_ptr& target) noexcept {
        __record_ptr record;
        bool should_wake = false;
        if (target) {
            std::lock_guard lk{mtx};
            auto it = fd_waiters.find(target->fd);
            if (it != fd_waiters.end()) {
                auto& waiters = it->second;
                auto& slot = target->kind == readiness::read
                    ? waiters.read
                    : waiters.write;
                if (slot == target) {
                    record = std::move(slot);
                    --pending;
                    update_interest_locked(it->first, waiters);
                    if (waiters.empty()) {
                        fd_waiters.erase(it);
                    }
                    should_wake = true;
                }
            }
        }
        if (should_wake) {
            wake_polling_thread();
        }
        if (record) {
            record->complete_stopped();
        }
    }

    void close() noexcept {
        {
            std::lock_guard lk{mtx};
            closed = true;
        }
        wake_polling_thread();
    }

    void request_stop() noexcept {
        __actions actions;
        {
            std::lock_guard lk{mtx};
            stopped = true;
            for (auto it = fd_waiters.begin(); it != fd_waiters.end();) {
                it = take_all_locked(it, actions, __completion_kind::stopped);
            }
        }
        wake_polling_thread();
        actions.run();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void run() noexcept {
        {
            std::lock_guard lk{mtx};
            poller_id = std::this_thread::get_id();
        }
        while (true) {
            int count = ::epoll_wait(
                epoll.get(), events.data(), static_cast<int>(events.size()), -1);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                complete_all_with_error(
                    __system_error(errno, "epoll_wait"));
                break;
            }

            __actions actions;
            bool should_exit = false;
            {
                std::lock_guard lk{mtx};
                for (int i = 0; i < count; ++i) {
                    const auto& event = events[static_cast<std::size_t>(i)];
                    if (event.data.fd == wake.get()) {
                        drain_wake_fd();
                        continue;
                    }

                    auto it = fd_waiters.find(event.data.fd);
                    if (it == fd_waiters.end()) {
                        continue;
                    }
                    take_ready_locked(it, event.events, actions);
                }

                if ((closed || stopped) && pending == 0) {
                    should_exit = true;
                }
            }
            actions.run();
            if (should_exit) {
                break;
            }
        }

        {
            std::lock_guard lk{mtx};
            poller_done = true;
        }
    }

    [[nodiscard]] auto called_from_poller() noexcept -> bool {
        std::lock_guard lk{mtx};
        return poller_id == std::this_thread::get_id();
    }

    void wake_polling_thread() noexcept {
        if (!wake) {
            return;
        }
        std::uint64_t value = 1;
        while (::write(wake.get(), &value, sizeof(value)) !=
               static_cast<ssize_t>(sizeof(value))) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF) {
                return;
            }
            return;
        }
    }

    void drain_wake_fd() noexcept {
        std::uint64_t value = 0;
        while (true) {
            const auto result = ::read(wake.get(), &value, sizeof(value));
            if (result == static_cast<ssize_t>(sizeof(value))) {
                continue;
            }
            if (result < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
    }

    bool update_interest_locked(int fd, __fd_waiters& waiters) noexcept {
        if (waiters.empty()) {
            if (waiters.registered) {
                if (::epoll_ctl(epoll.get(), EPOLL_CTL_DEL, fd, nullptr) != 0 &&
                    errno != ENOENT && errno != EBADF) {
                    return false;
                }
                waiters.registered = false;
            }
            return true;
        }

        epoll_event event{};
        event.events = waiters.events();
        event.data.fd = fd;
        const int op = waiters.registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        if (::epoll_ctl(epoll.get(), op, fd, &event) != 0) {
            const int update_error = errno;
            const int recovery_op = op == EPOLL_CTL_ADD && update_error == EEXIST
                ? EPOLL_CTL_MOD
                : op == EPOLL_CTL_MOD && update_error == ENOENT
                ? EPOLL_CTL_ADD
                : -1;
            if (recovery_op < 0 ||
                ::epoll_ctl(epoll.get(), recovery_op, fd, &event) != 0) {
                if (recovery_op < 0) {
                    errno = update_error;
                }
                return false;
            }
        }
        waiters.registered = true;
        return true;
    }

    auto take_all_locked(
        std::pmr::unordered_map<int, __fd_waiters>::iterator it,
        __actions& actions,
        __completion_kind completion)
            -> std::pmr::unordered_map<int, __fd_waiters>::iterator {
        auto& waiters = it->second;
        take_one_locked(waiters.read, actions, completion);
        take_one_locked(waiters.write, actions, completion);
        update_interest_locked(it->first, waiters);
        return fd_waiters.erase(it);
    }

    void take_ready_locked(
        std::pmr::unordered_map<int, __fd_waiters>::iterator it,
        std::uint32_t event_mask,
        __actions& actions) {
        auto& waiters = it->second;
        const bool read_ready =
            (event_mask & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
        const bool write_ready =
            (event_mask & (EPOLLOUT | EPOLLERR | EPOLLHUP)) != 0;

        if (read_ready) {
            take_one_locked(waiters.read, actions, __completion_kind::value);
        }
        if (write_ready) {
            take_one_locked(waiters.write, actions, __completion_kind::value);
        }

        update_interest_locked(it->first, waiters);
        if (waiters.empty()) {
            fd_waiters.erase(it);
        }
    }

    void take_one_locked(
        __record_ptr& slot,
        __actions& actions,
        __completion_kind completion) {
        if (!slot) {
            return;
        }
        auto record = std::move(slot);
        --pending;
        switch (completion) {
        case __completion_kind::value:
            actions.value(std::move(record));
            break;
        case __completion_kind::error:
            actions.error(std::move(record),
                __system_error(std::errc::io_error, "forge::io readiness error"));
            break;
        case __completion_kind::stopped:
            actions.stopped(std::move(record));
            break;
        }
    }

    [[nodiscard]] auto record_from_slot(
        __fd_waiters& waiters,
        __record_ptr& slot) noexcept -> __record_ptr {
        auto record = std::move(slot);
        update_interest_locked(record ? record->fd : -1, waiters);
        return record;
    }

    void complete_all_with_error(std::exception_ptr error) noexcept {
        __actions actions;
        {
            std::lock_guard lk{mtx};
            stopped = true;
            for (auto it = fd_waiters.begin(); it != fd_waiters.end();) {
                auto& waiters = it->second;
                if (waiters.read) {
                    --pending;
                    actions.error(std::move(waiters.read), error);
                }
                if (waiters.write) {
                    --pending;
                    actions.error(std::move(waiters.write), error);
                }
                update_interest_locked(it->first, waiters);
                it = fd_waiters.erase(it);
            }
        }
        actions.run();
    }

    std::pmr::memory_resource* memory;
    __fd epoll;
    __fd wake;
    std::mutex mtx;
    std::pmr::unordered_map<int, __fd_waiters> fd_waiters;
    std::pmr::vector<epoll_event> events;
    bool closed = false;
    bool stopped = false;
    bool poller_done = false;
    std::size_t pending = 0;
    std::thread::id poller_id{};
};

inline void __stop_callback_fn::operator()() const noexcept {
    auto rec = record.lock();
    if (rec) {
        rec->stop_requested.store(true, std::memory_order_release);
    }
    auto st = state.lock();
    if (st && rec) {
        // cancel_record can complete the record and reset this callback. Do
        // not touch callback members after this call returns.
        st->cancel_record(rec);
    }
}

template<class R>
bool __record<R>::install_stop_callback(
    std::weak_ptr<__state> state,
    std::weak_ptr<__record_base> self) noexcept {
    if (done.load(std::memory_order_acquire)) {
        return false;
    }
    if constexpr (requires {
                      std::any_stop_token{
                          std::execution::get_stop_token(
                              std::execution::get_env(rcvr))};
                  }) {
        try {
            auto token = std::any_stop_token{
                std::execution::get_stop_token(
                    std::execution::get_env(rcvr))};
            if (token.stop_possible()) {
                stop_callback.emplace(
                    std::move(token),
                    __stop_callback_fn{state, self});
            }
        } catch (...) {
            auto rec = self.lock();
            if (rec) {
                rec->complete_stopped();
            }
            return false;
        }
    }
    return true;
}

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    using record_t = __record<R>;

    __op(std::shared_ptr<__state> state, int fd, readiness kind, R rcvr)
        : state_(std::move(state))
        , record_(std::allocate_shared<record_t>(
              std::pmr::polymorphic_allocator<record_t>{state_->memory},
              fd,
              kind,
              std::move(rcvr))) {}

    __op(__op&&) = delete;
    auto operator=(__op&&) -> __op& = delete;
    __op(const __op&) = delete;
    auto operator=(const __op&) -> __op& = delete;

    void start() & noexcept {
        auto record = record_;
        auto state = state_;
        try {
            if (!state) {
                record->complete_stopped();
                return;
            }
            if (__stop_requested(record->rcvr)) {
                record->complete_stopped();
                return;
            }
            if (!record->install_stop_callback(state, record)) {
                return;
            }
            if (state->start(record)) {
                if (record->stop_requested.load(std::memory_order_acquire)) {
                    state->cancel_record(record);
                }
            }
        } catch (...) {
            record->complete_error(std::current_exception());
        }
    }

    std::shared_ptr<__state> state_;
    std::shared_ptr<record_t> record_;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;
    int fd = -1;
    readiness kind = readiness::read;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(state), fd, kind, std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, fd, kind, std::move(rcvr)};
    }
};

template<class R>
using __receiver_slot = std::shared_ptr<std::optional<R>>;

template<class R>
auto __make_receiver_slot(R&& rcvr)
    -> __receiver_slot<std::remove_cvref_t<R>> {
    using receiver_t = std::remove_cvref_t<R>;
    return std::make_shared<std::optional<receiver_t>>(std::forward<R>(rcvr));
}

template<class R>
[[nodiscard]] auto __has_receiver(const __receiver_slot<R>& slot) noexcept
    -> bool {
    return slot && slot->has_value();
}

template<class R>
auto __take_receiver(__receiver_slot<R>& slot) noexcept -> R {
    auto rcvr = std::move(**slot);
    slot->reset();
    return rcvr;
}

template<class R>
void __set_slot_value(__receiver_slot<R>& slot, std::size_t value) noexcept {
    if (__has_receiver(slot)) {
        std::execution::set_value(__take_receiver(slot), value);
    }
}

template<class R>
void __set_slot_error(
    __receiver_slot<R>& slot,
    std::exception_ptr ep) noexcept {
    if (__has_receiver(slot)) {
        std::execution::set_error(__take_receiver(slot), std::move(ep));
    }
}

template<class R>
void __set_slot_stopped(__receiver_slot<R>& slot) noexcept {
    if (__has_receiver(slot)) {
        std::execution::set_stopped(__take_receiver(slot));
    }
}

struct __read_operation {
    int fd = -1;
    std::span<std::byte> buffer{};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return buffer.empty();
    }

    [[nodiscard]] auto operator()() const -> std::size_t {
        return __read_some(fd, buffer);
    }
};

struct __write_operation {
    int fd = -1;
    std::span<const std::byte> buffer{};

    [[nodiscard]] auto empty() const noexcept -> bool {
        return buffer.empty();
    }

    [[nodiscard]] auto operator()() const -> std::size_t {
        return __write_some(fd, buffer);
    }
};

template<class R, class Operation>
struct __byte_receiver {
    using receiver_concept = std::execution::receiver_t;

    __receiver_slot<R> rcvr;
    Operation operation;

    void set_value() && noexcept {
        try {
            __set_slot_value(rcvr, operation());
        } catch (...) {
            __set_slot_error(rcvr, std::current_exception());
        }
    }

    void set_error(std::exception_ptr ep) && noexcept {
        __set_slot_error(rcvr, std::move(ep));
    }

    void set_stopped() && noexcept {
        __set_slot_stopped(rcvr);
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(**rcvr)))
        -> decltype(std::execution::get_env(**rcvr)) {
        return std::execution::get_env(**rcvr);
    }
};

template<class Operation>
struct __byte_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;
    int fd = -1;
    readiness kind = readiness::read;
    Operation operation;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(std::size_t),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct __op {
        using operation_state_concept =
            std::execution::operation_state_t;
        using receiver_t = __byte_receiver<R, Operation>;
        using inner_op_t = std::execution::connect_result_t<__sender, receiver_t>;
        struct inner_state_t {
            template<class Factory>
            explicit inner_state_t(Factory&& factory)
                : op(static_cast<Factory&&>(factory)()) {}

            inner_op_t op;
        };

        __op(
            std::shared_ptr<__state> st,
            int file,
            readiness ready,
            Operation op,
            R rcvr)
            : state(std::move(st))
            , fd(file)
            , kind(ready)
            , operation(op)
            , rcvr(__make_receiver_slot(std::move(rcvr)))
        {}

        __op(__op&&) = delete;
        auto operator=(__op&&) -> __op& = delete;
        __op(const __op&) = delete;
        auto operator=(const __op&) -> __op& = delete;

        void start() & noexcept {
            try {
                if (!state) {
                    __set_slot_stopped(rcvr);
                    return;
                }
                if (__stop_requested(**rcvr)) {
                    __set_slot_stopped(rcvr);
                    return;
                }
                if (operation.empty()) {
                    __set_slot_value(rcvr, 0);
                    return;
                }

                auto sender = __sender{state, fd, kind};
                inner = std::make_shared<inner_state_t>([&]() -> inner_op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{rcvr, operation});
                });
                auto keepalive = inner;
                std::execution::start(keepalive->op);
            } catch (...) {
                __set_slot_error(rcvr, std::current_exception());
            }
        }

        std::shared_ptr<__state> state;
        int fd = -1;
        readiness kind = readiness::read;
        Operation operation;
        __receiver_slot<R> rcvr;
        std::shared_ptr<inner_state_t> inner;
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(state),
            fd,
            kind,
            operation,
            std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, fd, kind, operation, std::move(rcvr)};
    }
};

} // namespace __detail

class context {
public:
    explicit context(context_options options = {})
        : state_(__make_state(options))
        , thread_([state = state_] { state->run(); }) {}

    ~context() noexcept {
        shutdown();
        wait();
    }

    context(const context&) = delete;
    auto operator=(const context&) -> context& = delete;
    context(context&&) = delete;
    auto operator=(context&&) -> context& = delete;

    [[nodiscard]] auto readable(int fd) -> __detail::__sender {
        return __detail::__sender{state_, fd, readiness::read};
    }

    [[nodiscard]] auto writable(int fd) -> __detail::__sender {
        return __detail::__sender{state_, fd, readiness::write};
    }

    [[nodiscard]] auto readable_typed(int fd) {
        return typed_detail::void_sender(readable(fd));
    }

    [[nodiscard]] auto writable_typed(int fd) {
        return typed_detail::void_sender(writable(fd));
    }

    [[nodiscard]] auto async_read_some(int fd, std::span<std::byte> buffer) {
        return __detail::__byte_sender<__detail::__read_operation>{
            state_,
            fd,
            readiness::read,
            __detail::__read_operation{fd, buffer}};
    }

    [[nodiscard]] auto async_read_some_typed(
        int fd,
        std::span<std::byte> buffer) {
        return typed_detail::size_sender(async_read_some(fd, buffer));
    }

    [[nodiscard]] auto async_write_some(
        int fd,
        std::span<const std::byte> buffer) {
        return __detail::__byte_sender<__detail::__write_operation>{
            state_,
            fd,
            readiness::write,
            __detail::__write_operation{fd, buffer}};
    }

    [[nodiscard]] auto async_write_some_typed(
        int fd,
        std::span<const std::byte> buffer) {
        return typed_detail::size_sender(async_write_some(fd, buffer));
    }

    void cancel(int fd) noexcept {
        state_->cancel(fd);
    }

    void close() noexcept {
        state_->close();
    }

    void request_stop() noexcept {
        state_->request_stop();
    }

    void shutdown() noexcept {
        state_->shutdown();
    }

    void wait() noexcept {
        if (!thread_.joinable()) {
            return;
        }
        if (state_->called_from_poller()) {
            thread_.detach();
            return;
        }
        thread_.join();
    }

private:
    static auto __make_state(context_options options)
        -> std::shared_ptr<__detail::__state> {
        options.memory = forge::normalize_memory_resource(options.memory);
        return std::allocate_shared<__detail::__state>(
            std::pmr::polymorphic_allocator<__detail::__state>{options.memory},
            options);
    }

    std::shared_ptr<__detail::__state> state_;
    std::thread thread_;
};

} // namespace forge::io

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
#include <forge/io/result.hpp>
#include <forge/resource_policy.hpp>

#include "linux_sigpipe_guard.hpp"

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif

namespace forge::io {

struct io_uring_context_options {
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    unsigned entries = 64;
};

namespace io_uring_detail {

[[noreturn]] inline void throw_system_error(int error, const char* operation) {
    throw std::system_error{error, std::generic_category(), operation};
}

[[noreturn]] inline void throw_unsupported(const char* reason) {
    throw std::system_error{
        std::make_error_code(std::errc::operation_not_supported),
        reason};
}

// Bounded flush retries for transient submission failures such as EAGAIN.
inline constexpr int flush_attempt_limit = 64;

// io_uring_enter can execute pipe writes inline on the entering thread, so a
// peer-closed pipe raises SIGPIPE against whichever thread flushed the SQE.
// Every enter is wrapped in the shared guard; construction failure (which
// pthread_sigmask cannot produce for valid arguments) degrades to the
// unguarded pre-fix behavior instead of throwing from noexcept paths.
class enter_sigpipe_guard {
public:
    enter_sigpipe_guard() noexcept {
        try {
            guard_.emplace();
        } catch (...) {
        }
    }

    void consume_generated_signal() noexcept {
        if (guard_.has_value()) {
            guard_->consume_generated_signal();
        }
    }

private:
    std::optional<__signal_detail::sigpipe_guard> guard_{};
};

class file_descriptor {
public:
    file_descriptor() noexcept = default;

    explicit file_descriptor(int value) noexcept
        : value_(value) {}

    ~file_descriptor() noexcept {
        reset();
    }

    file_descriptor(const file_descriptor&) = delete;
    auto operator=(const file_descriptor&) -> file_descriptor& = delete;

    file_descriptor(file_descriptor&& other) noexcept
        : value_(std::exchange(other.value_, -1)) {}

    auto operator=(file_descriptor&& other) noexcept -> file_descriptor& {
        if (this != &other) {
            reset(std::exchange(other.value_, -1));
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> int {
        return value_;
    }

    void reset(int next = -1) noexcept {
        if (value_ >= 0) {
            ::close(value_);
        }
        value_ = next;
    }

private:
    int value_ = -1;
};

class mapping {
public:
    mapping() noexcept = default;

    ~mapping() noexcept {
        reset();
    }

    mapping(const mapping&) = delete;
    auto operator=(const mapping&) -> mapping& = delete;
    mapping(mapping&&) = delete;
    auto operator=(mapping&&) -> mapping& = delete;

    void map(int fd, std::size_t size, std::uint64_t offset) {
        void* const address = ::mmap(
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            static_cast<off_t>(offset));
        if (address == MAP_FAILED) {
            throw_system_error(errno, "io_uring mmap");
        }
        address_ = address;
        size_ = size;
    }

    [[nodiscard]] auto get() const noexcept -> void* {
        return address_;
    }

    void reset() noexcept {
        if (address_ != MAP_FAILED) {
            ::munmap(address_, size_);
            address_ = MAP_FAILED;
            size_ = 0;
        }
    }

private:
    void* address_ = MAP_FAILED;
    std::size_t size_ = 0;
};

class ring {
public:
    ring(
        unsigned requested_entries,
        std::pmr::memory_resource* memory) {
        io_uring_params params{};
        const unsigned entries = requested_entries == 0 ? 1 : requested_entries;
        const int descriptor = static_cast<int>(::syscall(
            __NR_io_uring_setup,
            entries,
            &params));
        if (descriptor < 0) {
            throw_system_error(errno, "io_uring_setup");
        }
        descriptor_.reset(descriptor);

        verify_capabilities(params, memory);
        map_rings(params);
        verify_round_trip();
    }

    ring(const ring&) = delete;
    auto operator=(const ring&) -> ring& = delete;
    ring(ring&&) = delete;
    auto operator=(ring&&) -> ring& = delete;

    template<class Fill>
    [[nodiscard]] auto try_publish(Fill&& fill) noexcept -> bool {
        const unsigned head = load_acquire(sq_head_);
        const unsigned tail = load_relaxed(sq_tail_);
        if (tail - head >= *sq_ring_entries_) {
            return false;
        }

        const unsigned index = tail & *sq_ring_mask_;
        io_uring_sqe& sqe = sqes_[index];
        sqe = {};
        static_cast<Fill&&>(fill)(sqe);
        sq_array_[index] = index;
        store_release(sq_tail_, tail + 1);
        return true;
    }

    [[nodiscard]] auto publish_nop(std::uint64_t user_data) noexcept -> bool {
        return try_publish([user_data](io_uring_sqe& sqe) noexcept {
            sqe.opcode = IORING_OP_NOP;
            sqe.user_data = user_data;
        });
    }

    [[nodiscard]] auto publish_rw(
        std::uint8_t opcode,
        int fd,
        const void* address,
        unsigned length,
        std::uint64_t user_data) noexcept -> bool {
        return try_publish([=](io_uring_sqe& sqe) noexcept {
            sqe.opcode = opcode;
            sqe.fd = fd;
            sqe.addr = reinterpret_cast<std::uintptr_t>(address);
            sqe.len = length;
            // Stream/current-position one-shot IO per the frozen D3 subset.
            sqe.off = static_cast<std::uint64_t>(-1);
            sqe.user_data = user_data;
        });
    }

    [[nodiscard]] auto publish_cancel(
        std::uint64_t target_user_data,
        std::uint64_t cancel_user_data) noexcept -> bool {
        return try_publish([=](io_uring_sqe& sqe) noexcept {
            sqe.opcode = IORING_OP_ASYNC_CANCEL;
            sqe.fd = -1;
            sqe.addr = target_user_data;
            sqe.user_data = cancel_user_data;
        });
    }

    // Retracts the most recently published SQE only while the kernel head
    // proves that entry is still unconsumed. Every enter that can submit an
    // SQE is serialized by context_state::mutex, and SQPOLL is not enabled,
    // so the head cannot advance between this check and the tail update.
    [[nodiscard]] auto try_retract_last_unconsumed() noexcept -> bool {
        const unsigned head = load_acquire(sq_head_);
        const unsigned tail = load_relaxed(sq_tail_);
        if (head == tail) {
            return false;
        }
        store_release(sq_tail_, tail - 1U);
        return true;
    }

    // Success means the kernel consumed every published SQE: the published
    // tail was reached by the kernel head, which is the only authoritative
    // consumption signal across EINTR/EAGAIN retries. EBUSY is reported to
    // the caller because it implies a completion backlog that already
    // guarantees a poller wakeup.
    [[nodiscard]] auto flush_published() noexcept -> std::error_code {
        for (int attempt = 0; attempt < flush_attempt_limit;) {
            const unsigned head = load_acquire(sq_head_);
            const unsigned tail = load_relaxed(sq_tail_);
            if (head == tail) {
                return {};
            }

            enter_sigpipe_guard guard;
            const long result = ::syscall(
                __NR_io_uring_enter,
                descriptor_.get(),
                tail - head,
                0U,
                0U,
                nullptr,
                0U);
            const int error = result >= 0 ? 0 : errno;
            guard.consume_generated_signal();
            if (result >= 0) {
                ++attempt;
                continue;
            }
            if (error == EINTR || error == EAGAIN) {
                ++attempt;
                continue;
            }
            return {error, std::generic_category()};
        }
        return {EAGAIN, std::generic_category()};
    }

    // Waits for at least one CQE without consuming SQ entries. Submission is
    // serialized separately so a producer can prove whether its tail entry
    // was consumed before deciding whether rejection is still possible.
    [[nodiscard]] auto wait_for_completion() noexcept -> std::error_code {
        enter_sigpipe_guard guard;
        const long result = ::syscall(
            __NR_io_uring_enter,
            descriptor_.get(),
            0U,
            1U,
            IORING_ENTER_GETEVENTS,
            nullptr,
            0U);
        const int error = result >= 0 ? 0 : errno;
        guard.consume_generated_signal();
        if (error == 0) {
            return {};
        }
        return {error, std::generic_category()};
    }

    template<class Function>
    void drain_completions(Function&& function) noexcept {
        unsigned head = load_relaxed(cq_head_);
        const unsigned tail = load_acquire(cq_tail_);
        while (head != tail) {
            const io_uring_cqe completion =
                cqes_[head & *cq_ring_mask_];
            static_cast<Function&&>(function)(completion);
            ++head;
        }
        store_release(cq_head_, head);
    }

private:
    static auto load_acquire(unsigned* value) noexcept -> unsigned {
        return std::atomic_ref<unsigned>{*value}.load(
            std::memory_order_acquire);
    }

    static auto load_relaxed(unsigned* value) noexcept -> unsigned {
        return std::atomic_ref<unsigned>{*value}.load(
            std::memory_order_relaxed);
    }

    static void store_release(unsigned* value, unsigned next) noexcept {
        std::atomic_ref<unsigned>{*value}.store(
            next,
            std::memory_order_release);
    }

    void verify_capabilities(
        const io_uring_params& params,
        std::pmr::memory_resource* memory) {
        if ((params.features & IORING_FEAT_NODROP) == 0U) {
            throw_unsupported("io_uring requires IORING_FEAT_NODROP");
        }

        constexpr std::size_t probe_operations = IORING_OP_LAST;
        const std::size_t probe_bytes =
            sizeof(io_uring_probe) +
            probe_operations * sizeof(io_uring_probe_op);
        const std::size_t words =
            (probe_bytes + sizeof(std::uint64_t) - 1) /
            sizeof(std::uint64_t);
        std::pmr::vector<std::uint64_t> storage{
            words,
            std::uint64_t{},
            memory};
        auto* const probe =
            reinterpret_cast<io_uring_probe*>(storage.data());

        const int result = static_cast<int>(::syscall(
            __NR_io_uring_register,
            descriptor_.get(),
            IORING_REGISTER_PROBE,
            probe,
            static_cast<unsigned>(probe_operations)));
        if (result < 0) {
            const int error = errno;
            if (error == EINVAL ||
                error == ENOSYS ||
                error == EOPNOTSUPP) {
                throw_unsupported("io_uring runtime probing unavailable");
            }
            throw_system_error(error, "io_uring_register probe");
        }

        const auto supports = [probe](unsigned opcode) noexcept {
            for (unsigned index = 0; index < probe->ops_len; ++index) {
                const auto& operation = probe->ops[index];
                if (operation.op == opcode) {
                    return
                        (operation.flags & IO_URING_OP_SUPPORTED) != 0U;
                }
            }
            return false;
        };

        if (!supports(IORING_OP_NOP) ||
            !supports(IORING_OP_READ) ||
            !supports(IORING_OP_WRITE) ||
            !supports(IORING_OP_ASYNC_CANCEL)) {
            throw_unsupported("io_uring required opcode unavailable");
        }
    }

    void map_rings(const io_uring_params& params) {
        const std::size_t sq_ring_size =
            params.sq_off.array +
            params.sq_entries * sizeof(unsigned);
        const std::size_t cq_ring_size =
            params.cq_off.cqes +
            params.cq_entries * sizeof(io_uring_cqe);
        const bool single_mapping =
            (params.features & IORING_FEAT_SINGLE_MMAP) != 0U;

        if (single_mapping) {
            sq_ring_mapping_.map(
                descriptor_.get(),
                std::max(sq_ring_size, cq_ring_size),
                IORING_OFF_SQ_RING);
            sq_ring_ = sq_ring_mapping_.get();
            cq_ring_ = sq_ring_;
        } else {
            sq_ring_mapping_.map(
                descriptor_.get(),
                sq_ring_size,
                IORING_OFF_SQ_RING);
            cq_ring_mapping_.map(
                descriptor_.get(),
                cq_ring_size,
                IORING_OFF_CQ_RING);
            sq_ring_ = sq_ring_mapping_.get();
            cq_ring_ = cq_ring_mapping_.get();
        }

        sqes_mapping_.map(
            descriptor_.get(),
            params.sq_entries * sizeof(io_uring_sqe),
            IORING_OFF_SQES);

        auto* const sq_bytes = static_cast<std::byte*>(sq_ring_);
        auto* const cq_bytes = static_cast<std::byte*>(cq_ring_);
        sq_head_ = reinterpret_cast<unsigned*>(
            sq_bytes + params.sq_off.head);
        sq_tail_ = reinterpret_cast<unsigned*>(
            sq_bytes + params.sq_off.tail);
        sq_ring_mask_ = reinterpret_cast<unsigned*>(
            sq_bytes + params.sq_off.ring_mask);
        sq_ring_entries_ = reinterpret_cast<unsigned*>(
            sq_bytes + params.sq_off.ring_entries);
        sq_array_ = reinterpret_cast<unsigned*>(
            sq_bytes + params.sq_off.array);
        cq_head_ = reinterpret_cast<unsigned*>(
            cq_bytes + params.cq_off.head);
        cq_tail_ = reinterpret_cast<unsigned*>(
            cq_bytes + params.cq_off.tail);
        cq_ring_mask_ = reinterpret_cast<unsigned*>(
            cq_bytes + params.cq_off.ring_mask);
        cqes_ = reinterpret_cast<io_uring_cqe*>(
            cq_bytes + params.cq_off.cqes);
        sqes_ = static_cast<io_uring_sqe*>(sqes_mapping_.get());
    }

    // A constructed ring must be proven functional. Sandboxes can permit
    // io_uring_setup and io_uring_register while denying io_uring_enter, and
    // discovering that only inside the poller would leave callers with a
    // successfully constructed but dead context.
    void verify_round_trip() {
        if (!publish_nop(0)) {
            throw_unsupported("io_uring submission queue unavailable");
        }
        const std::error_code flush_error = flush_published();
        if (flush_error) {
            throw std::system_error{flush_error, "io_uring_enter submit"};
        }

        std::error_code wait_error;
        do {
            wait_error = wait_for_completion();
        } while (wait_error.value() == EINTR);
        if (wait_error) {
            throw std::system_error{wait_error, "io_uring_enter getevents"};
        }

        bool observed = false;
        bool foreign = false;
        drain_completions([&](const io_uring_cqe& completion) noexcept {
            if (completion.user_data == 0 && completion.res == 0) {
                observed = true;
            } else {
                foreign = true;
            }
        });
        if (!observed || foreign) {
            throw_unsupported("io_uring NOP round trip failed");
        }
    }

    file_descriptor descriptor_;
    mapping sq_ring_mapping_;
    mapping cq_ring_mapping_;
    mapping sqes_mapping_;
    void* sq_ring_ = nullptr;
    void* cq_ring_ = nullptr;
    unsigned* sq_head_ = nullptr;
    unsigned* sq_tail_ = nullptr;
    unsigned* sq_ring_mask_ = nullptr;
    unsigned* sq_ring_entries_ = nullptr;
    unsigned* sq_array_ = nullptr;
    unsigned* cq_head_ = nullptr;
    unsigned* cq_tail_ = nullptr;
    unsigned* cq_ring_mask_ = nullptr;
    io_uring_cqe* cqes_ = nullptr;
    io_uring_sqe* sqes_ = nullptr;
};

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
using operation_continuation = __coro_detail::resume_target;
#else
using operation_continuation = void*;
#endif

enum class operation_phase {
    starting,
    suspended,
    completed
};

enum class submit_status {
    accepted,
    stopped,
    saturated
};

// Borrowed operation state embedded in the awaiting coroutine frame. The
// address doubles as the unique CQE user_data, so it must stay valid until
// every expected CQE is drained; bit 0 tags cancel CQEs and is never set on
// an aligned operation address.
//
// Once a cancel SQE referencing this address has been published, the
// operation retires only after both the target CQE and the cancel CQE are
// drained. Delaying the resume until then keeps the address from being
// reused by a follow-up operation while the kernel still holds a cancel
// request against the old user_data value.
struct io_operation {
    io_operation* prev = nullptr;
    io_operation* next = nullptr;
    operation_continuation continuation{};
    std::atomic<operation_phase> phase{operation_phase::starting};
    std::int32_t result = 0;
    std::uint8_t pending_cqes = 1;
    bool registered = false;
    bool cancel_requested = false;
    bool cancel_published = false;
};

static_assert(alignof(io_operation) >= 2);

struct context_state {
    explicit context_state(io_uring_context_options options)
        : memory(forge::normalize_memory_resource(options.memory))
        , ring_state(options.entries, memory)
    {}

    // Lifecycle entry points stay idempotent but always re-attempt the
    // wakeup so that a previously failed flush gets retried by any later
    // transition, including the destructor path.
    void close() noexcept {
        std::lock_guard lock{mutex};
        closed = true;
        submit_wakeup_locked();
        cv.notify_all();
    }

    void request_stop() noexcept {
        std::lock_guard lock{mutex};
        stopped = true;
        cancel_all_locked();
        submit_wakeup_locked();
        cv.notify_all();
    }

    void shutdown() noexcept {
        std::lock_guard lock{mutex};
        closed = true;
        stopped = true;
        cancel_all_locked();
        submit_wakeup_locked();
        cv.notify_all();
    }

    // Blocks until the poller has committed to exiting. A wakeup NOP whose
    // flush failed hard (e.g. transient ENOMEM) stays parked in the SQ while
    // the poller blocks in GETEVENTS with nothing else in flight; a blind
    // join would then hang forever because nothing retries the flush. This
    // loop re-drives the whole wakeup chain (publish if the NOP could not
    // even be published into a saturated SQ, then flush) until the kernel
    // accepts it; the flush inside also consumes any parked entries that
    // were keeping the SQ full.
    void wait_poller_exit() noexcept {
        std::unique_lock lock{mutex};
        while (!poller_done) {
            if (closed || stopped) {
                submit_wakeup_locked();
                if (wakeup_in_flight) {
                    // The NOP reached the kernel; its CQE will wake the
                    // poller, which then exits and notifies.
                    cv.wait(lock);
                } else {
                    cv.wait_for(lock, std::chrono::milliseconds{1});
                }
            } else {
                cv.wait(lock);
            }
        }
    }

    void run() noexcept {
        {
            std::lock_guard lock{mutex};
            poller_id = std::this_thread::get_id();
        }

        for (;;) {
            std::error_code flush_error;
            {
                std::lock_guard lock{mutex};
                flush_error = ring_state.flush_published();
                record_flush_locked(flush_error);
            }
            if (flush_error && flush_error.value() != EBUSY) {
                // A producer or lifecycle pump remains responsible for
                // waking a poller already blocked in GETEVENTS. Once awake,
                // retry parked entries without holding the lock across a
                // bounded backoff.
                std::this_thread::sleep_for(
                    std::chrono::microseconds{100});
                continue;
            }

            const std::error_code wait_error =
                ring_state.wait_for_completion();
            if (wait_error) {
                const int error = wait_error.value();
                if (error != EINTR && error != EAGAIN && error != EBUSY) {
                    std::lock_guard lock{mutex};
                    poller_error = wait_error;
                    poller_done = true;
                    cv.notify_all();
                    break;
                }
            }

            ring_state.drain_completions(
                [this](const io_uring_cqe& completion) noexcept {
                    complete(completion);
                });

            io_operation* ready = nullptr;
            {
                std::lock_guard lock{mutex};
                ready = ready_head;
                ready_head = nullptr;
                ready_tail = nullptr;
            }
            // Resumptions run outside the backend lock per the frozen D1
            // protocol. A resumed coroutine may destroy its operation state
            // immediately, so successor and continuation are read first.
            while (ready != nullptr) {
                io_operation* const next = ready->next;
                const auto continuation = ready->continuation;
                ready->next = nullptr;
                resume_continuation(continuation);
                ready = next;
            }

            bool should_exit = false;
            {
                std::lock_guard lock{mutex};
                retry_parked_cancels_locked();
                should_exit =
                    (closed || stopped) &&
                    operations_head == nullptr &&
                    !wakeup_in_flight;
                if (should_exit) {
                    poller_done = true;
                    cv.notify_all();
                }
            }
            if (should_exit) {
                break;
            }
        }
    }

    [[nodiscard]] auto submit_operation(
        io_operation& operation,
        std::uint8_t opcode,
        int fd,
        const void* address,
        unsigned length) noexcept -> submit_status {
        std::lock_guard lock{mutex};
        if (closed || stopped || poller_done) {
            return submit_status::stopped;
        }

        const std::uint64_t user_data = operation_user_data(operation);
        if (!ring_state.publish_rw(opcode, fd, address, length, user_data)) {
            // The SQ frees slots on consumption, so one flush normally
            // empties it; a second failure means the kernel is refusing
            // submissions right now.
            record_flush_locked(ring_state.flush_published());
            if (!ring_state.publish_rw(
                    opcode, fd, address, length, user_data)) {
                return submit_status::saturated;
            }
        }

        const std::error_code flush_error = ring_state.flush_published();
        record_flush_locked(flush_error);
        if (flush_error && flush_error.value() != EBUSY) {
            // Reject only while the shared head proves this tail entry was
            // not consumed. If the syscall consumed it before reporting an
            // error, ownership has transferred to the kernel and the
            // operation must remain registered for its CQE.
            if (ring_state.try_retract_last_unconsumed()) {
                return submit_status::saturated;
            }
        }
        link_operation_locked(operation);
        return submit_status::accepted;
    }

    void request_cancel(io_operation& operation) noexcept {
        std::lock_guard lock{mutex};
        if (!operation.registered || operation.cancel_requested) {
            return;
        }
        operation.cancel_requested = true;
        queue_cancel_locked(operation);
    }

    [[nodiscard]] auto called_from_poller() noexcept -> bool {
        std::lock_guard lock{mutex};
        return poller_id == std::this_thread::get_id();
    }

    [[nodiscard]] auto last_error() noexcept -> std::error_code {
        std::lock_guard lock{mutex};
        return poller_error;
    }

    [[nodiscard]] auto last_flush_diagnostic() noexcept -> std::error_code {
        std::lock_guard lock{mutex};
        return flush_diagnostic;
    }

    std::pmr::memory_resource* memory;
    ring ring_state;
    std::mutex mutex;
    std::condition_variable cv;
    io_operation* operations_head = nullptr;
    io_operation* ready_head = nullptr;
    io_operation* ready_tail = nullptr;
    std::size_t parked_cancels = 0;
    bool closed = false;
    bool stopped = false;
    bool wakeup_published = false;
    bool wakeup_in_flight = false;
    bool poller_done = false;
    std::thread::id poller_id{};
    // poller_error records only hard failures that stop the poller; every
    // recoverable or transient observation (flush retries, wakeup
    // saturation, odd administrative CQEs) goes into flush_diagnostic so a
    // busy-but-healthy ring never reports backend death.
    std::error_code poller_error{};
    std::error_code flush_diagnostic{};

private:
    static constexpr std::uint64_t cancel_tag = 1;

    static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));

    [[nodiscard]] auto wakeup_user_data() const noexcept -> std::uint64_t {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(this));
    }

    [[nodiscard]] static auto operation_user_data(
        const io_operation& operation) noexcept -> std::uint64_t {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&operation));
    }

    void link_operation_locked(io_operation& operation) noexcept {
        operation.prev = nullptr;
        operation.next = operations_head;
        if (operations_head != nullptr) {
            operations_head->prev = &operation;
        }
        operations_head = &operation;
        operation.registered = true;
    }

    void unlink_operation_locked(io_operation& operation) noexcept {
        if (operation.prev != nullptr) {
            operation.prev->next = operation.next;
        } else {
            operations_head = operation.next;
        }
        if (operation.next != nullptr) {
            operation.next->prev = operation.prev;
        }
        operation.prev = nullptr;
        operation.next = nullptr;
        operation.registered = false;
    }

    // Flush failures after a successful publish are recoverable: the SQE
    // stays published and every later flush (submissions, wakeups, and the
    // poller wait itself) retries it, so only the diagnostic is recorded.
    void record_flush_locked(std::error_code flush_error) noexcept {
        if (!flush_error) {
            flush_diagnostic = {};
        } else if (flush_error.value() != EBUSY) {
            flush_diagnostic = flush_error;
        }
    }

    // The parked state is registry-resident bookkeeping per the frozen D5
    // retry-queue requirement: an unpublishable cancel never blocks and is
    // implicitly discarded when the target retires before it was published,
    // so it can never reference a reused operation address.
    [[nodiscard]] auto publish_cancel_locked(io_operation& operation) noexcept
        -> bool {
        const std::uint64_t target = operation_user_data(operation);
        if (!ring_state.publish_cancel(target, target | cancel_tag)) {
            return false;
        }
        operation.cancel_published = true;
        ++operation.pending_cqes;
        return true;
    }

    void queue_cancel_locked(io_operation& operation) noexcept {
        if (!publish_cancel_locked(operation)) {
            ++parked_cancels;
            return;
        }
        const std::error_code flush_error = ring_state.flush_published();
        record_flush_locked(flush_error);
        if (flush_error && flush_error.value() != EBUSY) {
            // The poller may already be blocked in GETEVENTS with no CQE that
            // can expose this parked cancel. Publish a NOP and re-drive the
            // shared SQ immediately; transient hard failures then cannot turn
            // an accepted stop request into a permanent sleep.
            submit_wakeup_locked();
        }
    }

    void retry_parked_cancels_locked() noexcept {
        if (parked_cancels == 0) {
            return;
        }
        std::size_t published = 0;
        for (io_operation* operation = operations_head;
             operation != nullptr && parked_cancels != 0;
             operation = operation->next) {
            if (!operation->cancel_requested ||
                operation->cancel_published) {
                continue;
            }
            if (!publish_cancel_locked(*operation)) {
                break;
            }
            --parked_cancels;
            ++published;
        }
        if (published != 0) {
            record_flush_locked(ring_state.flush_published());
        }
    }

    void cancel_all_locked() noexcept {
        for (io_operation* operation = operations_head;
             operation != nullptr;
             operation = operation->next) {
            if (!operation->cancel_requested) {
                operation->cancel_requested = true;
                queue_cancel_locked(*operation);
            }
        }
    }

    static void resume_continuation(
        operation_continuation continuation) noexcept {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
        continuation.resume();
#else
        (void)continuation;
#endif
    }

    void submit_wakeup_locked() noexcept {
        if (poller_done || wakeup_in_flight) {
            return;
        }

        if (!wakeup_published) {
            if (!ring_state.publish_nop(wakeup_user_data())) {
                record_flush_locked(ring_state.flush_published());
                if (!ring_state.publish_nop(wakeup_user_data())) {
                    // A saturated SQ implies in-flight operations, so their
                    // CQEs already guarantee a poller wakeup; the next
                    // lifecycle transition retries this publish.
                    flush_diagnostic =
                        std::make_error_code(std::errc::no_buffer_space);
                    return;
                }
            }
            wakeup_published = true;
        }

        const std::error_code flush_error = ring_state.flush_published();
        if (!flush_error) {
            wakeup_published = false;
            wakeup_in_flight = true;
            return;
        }
        if (flush_error.value() == EBUSY) {
            // A completion backlog is pending, so the poller is already
            // guaranteed to wake up; the published SQE stays queued for a
            // later flush attempt.
            return;
        }
        // The wakeup NOP stays parked; wait_poller_exit() keeps re-flushing
        // it, so this failure is a retried diagnostic rather than death.
        flush_diagnostic = flush_error;
    }

    void complete(const io_uring_cqe& completion) noexcept {
        std::lock_guard lock{mutex};
        if (completion.user_data == wakeup_user_data()) {
            // A serialized poller flush may consume a parked wakeup before
            // the lifecycle thread observes that its own retry succeeded,
            // so the CQE clears both ownership flags.
            wakeup_in_flight = false;
            wakeup_published = false;
            if (completion.res < 0) {
                flush_diagnostic = {
                    -completion.res,
                    std::generic_category()};
            } else {
                // A successful wakeup round trip proves the submission path
                // recovered; clear the diagnostic so readers can tell live
                // stress from stale history.
                flush_diagnostic = {};
            }
            return;
        }

        if ((completion.user_data & cancel_tag) != 0) {
            // Administrative drain per the frozen D5 semantics: a cancel CQE
            // never produces a user terminal. The target operation is still
            // registered because retirement waits for this CQE.
            auto* const operation = reinterpret_cast<io_operation*>(
                static_cast<std::uintptr_t>(
                    completion.user_data & ~cancel_tag));
            if (completion.res < 0 &&
                completion.res != -ENOENT &&
                completion.res != -EALREADY) {
                flush_diagnostic = {
                    -completion.res,
                    std::generic_category()};
            }
            retire_if_drained_locked(*operation);
            return;
        }

        auto* const operation = reinterpret_cast<io_operation*>(
            static_cast<std::uintptr_t>(completion.user_data));
        operation->result = completion.res;
        retire_if_drained_locked(*operation);
    }

    void retire_if_drained_locked(io_operation& operation) noexcept {
        if (--operation.pending_cqes != 0) {
            return;
        }
        if (operation.cancel_requested && !operation.cancel_published &&
            parked_cancels != 0) {
            // The parked cancel dies with the registry entry before it was
            // ever published, so no kernel-side reference remains.
            --parked_cancels;
        }
        unlink_operation_locked(operation);
        if (operation.phase.exchange(
                operation_phase::completed,
                std::memory_order_acq_rel) == operation_phase::suspended) {
            // Reuse the unlinked node as an intrusive ready-list entry so
            // resumption needs no allocation on the completion path.
            operation.next = nullptr;
            if (ready_tail == nullptr) {
                ready_head = &operation;
            } else {
                ready_tail->next = &operation;
            }
            ready_tail = &operation;
        }
    }
};

} // namespace io_uring_detail

class io_uring_context {
public:
    explicit io_uring_context(io_uring_context_options options = {})
        : state_(make_state(options))
        , thread_([state = state_] { state->run(); })
    {}

    ~io_uring_context() noexcept {
        shutdown();
        wait();
    }

    io_uring_context(const io_uring_context&) = delete;
    auto operator=(const io_uring_context&) -> io_uring_context& = delete;
    io_uring_context(io_uring_context&&) = delete;
    auto operator=(io_uring_context&&) -> io_uring_context& = delete;

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
        // Pump parked wakeup flush retries until the poller commits to
        // exiting, then join; see wait_poller_exit() for the hang this
        // avoids.
        state_->wait_poller_exit();
        thread_.join();
    }

    // Backend-health diagnostic. A default (zero) error code means the
    // poller has not recorded a hard failure. After a non-recoverable
    // io_uring_enter failure the poller exits, later submissions complete
    // stopped, and this accessor is the way to distinguish that backend
    // death from a graceful request_stop()/close() drain. Recoverable
    // observations never land here; see last_flush_diagnostic().
    [[nodiscard]] auto last_error() const noexcept -> std::error_code {
        return state_->last_error();
    }

    // Most recent recoverable observation (retried flush errors, transient
    // wakeup saturation, unexpected administrative CQE results). Non-zero
    // values here describe a busy or stressed ring, not backend death; the
    // poller keeps running and operations keep completing. A later successful
    // flush or wakeup round trip clears it, so a stale non-zero value does not
    // outlive the recovery it diagnosed.
    [[nodiscard]] auto last_flush_diagnostic() const noexcept
        -> std::error_code {
        return state_->last_flush_diagnostic();
    }

    // Internal accessor for the coroutine-native operation surface.
    [[nodiscard]] auto __state() noexcept -> io_uring_detail::context_state& {
        return *state_;
    }

private:
    static auto make_state(io_uring_context_options options)
        -> std::shared_ptr<io_uring_detail::context_state> {
        options.memory = forge::normalize_memory_resource(options.memory);
        return std::allocate_shared<io_uring_detail::context_state>(
            std::pmr::polymorphic_allocator<
                io_uring_detail::context_state>{options.memory},
            options);
    }

    std::shared_ptr<io_uring_detail::context_state> state_;
    std::thread thread_;
};

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace io_uring_detail {

struct cancel_requester {
    context_state* state = nullptr;
    io_operation* operation = nullptr;

    auto operator()() const noexcept -> void {
        state->request_cancel(*operation);
    }
};

// Direct io_awaitable operation state for one-shot stream reads/writes.
// The awaitable, the fd and the buffer are borrowed and must stay valid
// until await_resume(); a suspended operation must not be abandoned before
// its target CQE is drained.
template<bool IsRead>
class rw_some_awaitable {
public:
    rw_some_awaitable(
        context_state& state,
        int fd,
        const void* address,
        std::size_t size) noexcept
        : state_(&state)
        , address_(address)
        , size_(size)
        , fd_(fd)
    {}

    rw_some_awaitable(rw_some_awaitable&& other)
        : state_(other.take_unstarted())
        , address_(other.address_)
        , size_(other.size_)
        , fd_(other.fd_)
    {}

    auto operator=(rw_some_awaitable&&) -> rw_some_awaitable& = delete;
    rw_some_awaitable(const rw_some_awaitable&) = delete;
    auto operator=(const rw_some_awaitable&) -> rw_some_awaitable& = delete;

    ~rw_some_awaitable() {
        // Abandoning a submitted operation before await_resume would leave
        // the kernel and the context registry holding references into this
        // frame and the borrowed buffer, so a later completion writes into
        // freed memory. Mirror the erased-stream guard and fail fast
        // instead.
        //
        // The guard is deliberately phase-agnostic: even after the target
        // CQE drained, a not-yet-resumed operation sits on the poller's
        // intrusive ready list (or in its popped local batch), whose links
        // and continuation live inside this frame, so destruction in that
        // window still leaves the poller walking freed memory. There is no
        // submitted-but-unresumed state in which destruction is safe.
        if (outcome_ == outcome::submitted && !resumed_) {
            std::terminate();
        }
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> continuation, const io_env* env)
        -> bool {
        return await_suspend(
            __coro_detail::resume_target{continuation, nullptr}, env);
    }

    auto await_suspend(
        __coro_detail::resume_target continuation,
        const io_env* env) -> bool {
        if (env != nullptr && env->stop_token.stop_requested()) {
            outcome_ = outcome::stopped;
            return false;
        }

        // Empty buffers complete inline with value 0 and never reach the
        // ring; the epoll precedent orders this after the stop check but
        // before any context consultation, so a closed context still
        // observes value 0 here.
        if (size_ == 0) {
            outcome_ = outcome::inline_value;
            return false;
        }

        operation_.continuation = continuation;
        const submit_status status = state_->submit_operation(
            operation_,
            IsRead ? IORING_OP_READ : IORING_OP_WRITE,
            fd_,
            address_,
            clamped_length());
        if (status == submit_status::stopped) {
            outcome_ = outcome::stopped;
            return false;
        }
        if (status == submit_status::saturated) {
            outcome_ = outcome::saturated;
            return false;
        }

        outcome_ = outcome::submitted;
        if (env != nullptr && env->stop_token.stop_possible()) {
            stop_callback_.emplace(
                env->stop_token,
                cancel_requester{state_, &operation_});
        }
        return operation_.phase.exchange(
            operation_phase::suspended,
            std::memory_order_acq_rel) != operation_phase::completed;
    }

    [[nodiscard]] auto await_resume() -> io_result<std::size_t> {
        resumed_ = true;
        stop_callback_.reset();

        switch (outcome_) {
        case outcome::not_started:
        case outcome::inline_value:
            return io_result<std::size_t>::success(0);
        case outcome::stopped:
            throw sender_stopped{};
        case outcome::saturated:
            return io_result<std::size_t>::failure(
                std::make_error_code(std::errc::no_buffer_space),
                0);
        case outcome::submitted:
            break;
        }

        const std::int32_t result = operation_.result;
        if (result >= 0) {
            if (IsRead && result == 0) {
                return io_result<std::size_t>::end_of_file(0);
            }
            return io_result<std::size_t>::success(
                static_cast<std::size_t>(result));
        }
        if (result == -ECANCELED && operation_.cancel_requested) {
            throw sender_stopped{};
        }
        return io_result<std::size_t>::failure(
            {-result, std::generic_category()},
            0);
    }

private:
    enum class outcome {
        not_started,
        inline_value,
        stopped,
        saturated,
        submitted
    };

    [[nodiscard]] auto take_unstarted() -> context_state* {
        if (outcome_ != outcome::not_started ||
            operation_.phase.load(std::memory_order_acquire) !=
                operation_phase::starting) {
            throw std::logic_error{
                "forge::io io_uring awaitable cannot move after suspension"};
        }
        return state_;
    }

    [[nodiscard]] auto clamped_length() const noexcept -> unsigned {
        // The SQE length field and the CQE result are 32-bit; oversized
        // spans submit a legal short IO instead of wrapping.
        constexpr std::size_t max_length = static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max());
        return static_cast<unsigned>(std::min(size_, max_length));
    }

    context_state* state_;
    const void* address_;
    std::size_t size_;
    int fd_;
    io_operation operation_{};
    outcome outcome_ = outcome::not_started;
    bool resumed_ = false;
    std::optional<std::inplace_stop_callback<cancel_requester>> stop_callback_{};
};

} // namespace io_uring_detail

[[nodiscard]] inline auto async_read_some(
    io_uring_context& context,
    int fd,
    std::span<std::byte> buffer) noexcept
    -> io_uring_detail::rw_some_awaitable<true> {
    return {context.__state(), fd, buffer.data(), buffer.size()};
}

[[nodiscard]] inline auto async_write_some(
    io_uring_context& context,
    int fd,
    std::span<const std::byte> buffer) noexcept
    -> io_uring_detail::rw_some_awaitable<false> {
    return {context.__state(), fd, buffer.data(), buffer.size()};
}

#endif // __cpp_impl_coroutine

} // namespace forge::io

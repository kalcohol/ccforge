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

#include <forge/resource_policy.hpp>

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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

    [[nodiscard]] auto publish_nop(std::uint64_t user_data) noexcept -> bool {
        const unsigned head = load_acquire(sq_head_);
        const unsigned tail = load_relaxed(sq_tail_);
        if (tail - head >= *sq_ring_entries_) {
            return false;
        }

        const unsigned index = tail & *sq_ring_mask_;
        io_uring_sqe& sqe = sqes_[index];
        sqe = {};
        sqe.opcode = IORING_OP_NOP;
        sqe.user_data = user_data;
        sq_array_[index] = index;
        store_release(sq_tail_, tail + 1);
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

            const long result = ::syscall(
                __NR_io_uring_enter,
                descriptor_.get(),
                tail - head,
                0U,
                0U,
                nullptr,
                0U);
            if (result >= 0) {
                ++attempt;
                continue;
            }
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            if (error == EAGAIN) {
                ++attempt;
                std::this_thread::yield();
                continue;
            }
            return {error, std::generic_category()};
        }
        return {EAGAIN, std::generic_category()};
    }

    [[nodiscard]] auto wait_for_completion() noexcept -> std::error_code {
        const long result = ::syscall(
            __NR_io_uring_enter,
            descriptor_.get(),
            0U,
            1U,
            IORING_ENTER_GETEVENTS,
            nullptr,
            0U);
        if (result >= 0) {
            return {};
        }
        return {errno, std::generic_category()};
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

enum class completion_kind {
    wakeup
};

struct completion_record {
    completion_kind kind = completion_kind::wakeup;
};

using completion_record_ptr = std::shared_ptr<completion_record>;

struct context_state {
    explicit context_state(io_uring_context_options options)
        : memory(forge::normalize_memory_resource(options.memory))
        , ring_state(options.entries, memory)
        , pending_records(
              0,
              std::hash<std::uint64_t>{},
              std::equal_to<std::uint64_t>{},
              std::pmr::polymorphic_allocator<
                  std::pair<const std::uint64_t, completion_record_ptr>>{
                  memory})
        , wakeup_record(std::allocate_shared<completion_record>(
              std::pmr::polymorphic_allocator<completion_record>{memory}))
    {}

    // Lifecycle entry points stay idempotent but always re-attempt the
    // wakeup so that a previously failed flush gets retried by any later
    // transition, including the destructor path.
    void close() noexcept {
        std::lock_guard lock{mutex};
        closed = true;
        submit_wakeup_locked();
    }

    void request_stop() noexcept {
        std::lock_guard lock{mutex};
        stopped = true;
        submit_wakeup_locked();
    }

    void shutdown() noexcept {
        std::lock_guard lock{mutex};
        closed = true;
        stopped = true;
        submit_wakeup_locked();
    }

    void run() noexcept {
        {
            std::lock_guard lock{mutex};
            poller_id = std::this_thread::get_id();
        }

        for (;;) {
            const std::error_code wait_error =
                ring_state.wait_for_completion();
            if (wait_error && wait_error.value() != EINTR) {
                std::lock_guard lock{mutex};
                poller_error = wait_error;
                poller_done = true;
                break;
            }

            ring_state.drain_completions(
                [this](const io_uring_cqe& completion) noexcept {
                    complete(completion);
                });

            bool should_exit = false;
            {
                std::lock_guard lock{mutex};
                should_exit =
                    (closed || stopped) &&
                    pending_records.empty() &&
                    !wakeup_in_flight;
                if (should_exit) {
                    poller_done = true;
                }
            }
            if (should_exit) {
                break;
            }
        }
    }

    [[nodiscard]] auto called_from_poller() noexcept -> bool {
        std::lock_guard lock{mutex};
        return poller_id == std::this_thread::get_id();
    }

    std::pmr::memory_resource* memory;
    ring ring_state;
    std::mutex mutex;
    std::pmr::unordered_map<std::uint64_t, completion_record_ptr>
        pending_records;
    completion_record_ptr wakeup_record;
    bool closed = false;
    bool stopped = false;
    bool wakeup_published = false;
    bool wakeup_in_flight = false;
    bool poller_done = false;
    std::thread::id poller_id{};
    std::error_code poller_error{};

private:
    [[nodiscard]] auto wakeup_user_data() const noexcept -> std::uint64_t {
        static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(wakeup_record.get()));
    }

    void submit_wakeup_locked() noexcept {
        if (poller_done || wakeup_in_flight) {
            return;
        }

        if (!wakeup_published) {
            if (!ring_state.publish_nop(wakeup_user_data())) {
                // Unreachable while the wakeup NOP is the only submission;
                // phase 2 must replace this with a context-owned retry
                // queue before adding data-path submissions.
                poller_error =
                    std::make_error_code(std::errc::no_buffer_space);
                return;
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
        poller_error = flush_error;
    }

    void complete(const io_uring_cqe& completion) noexcept {
        std::lock_guard lock{mutex};
        if (completion.user_data == wakeup_user_data()) {
            wakeup_in_flight = false;
            if (completion.res < 0) {
                poller_error = {
                    -completion.res,
                    std::generic_category()};
            }
            return;
        }

        const auto iterator = pending_records.find(completion.user_data);
        if (iterator != pending_records.end()) {
            pending_records.erase(iterator);
            return;
        }
        poller_error = std::make_error_code(std::errc::io_error);
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
        thread_.join();
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

} // namespace forge::io

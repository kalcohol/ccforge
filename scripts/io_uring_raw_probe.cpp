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

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

inline constexpr int unavailable_exit_code = 77;

[[nodiscard]] auto unavailable_error(int error) noexcept -> bool {
    return error == ENOSYS || error == EPERM || error == EACCES;
}

} // namespace

int main() {
    io_uring_params params{};
    const int ring_fd = static_cast<int>(
        ::syscall(__NR_io_uring_setup, 8U, &params));
    if (ring_fd < 0) {
        const int error = errno;
        std::fprintf(
            stderr,
            "io_uring_setup: %s (%d)\n",
            std::strerror(error),
            error);
        return unavailable_error(error) ? unavailable_exit_code : 10;
    }

    const std::size_t sq_ring_size = params.sq_off.array +
        params.sq_entries * sizeof(unsigned);
    const std::size_t cq_ring_size = params.cq_off.cqes +
        params.cq_entries * sizeof(io_uring_cqe);
    const bool single_map =
        (params.features & IORING_FEAT_SINGLE_MMAP) != 0U;
    const std::size_t shared_size = std::max(
        sq_ring_size,
        cq_ring_size);

    void* const sq_ring = ::mmap(
        nullptr,
        single_map ? shared_size : sq_ring_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        ring_fd,
        IORING_OFF_SQ_RING);
    if (sq_ring == MAP_FAILED) {
        std::fprintf(stderr, "mmap SQ: %s\n", std::strerror(errno));
        ::close(ring_fd);
        return 11;
    }

    void* cq_ring = sq_ring;
    if (!single_map) {
        cq_ring = ::mmap(
            nullptr,
            cq_ring_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE,
            ring_fd,
            IORING_OFF_CQ_RING);
        if (cq_ring == MAP_FAILED) {
            std::fprintf(stderr, "mmap CQ: %s\n", std::strerror(errno));
            ::munmap(sq_ring, sq_ring_size);
            ::close(ring_fd);
            return 12;
        }
    }

    const std::size_t sqes_size =
        params.sq_entries * sizeof(io_uring_sqe);
    void* const sqes_mapping = ::mmap(
        nullptr,
        sqes_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        ring_fd,
        IORING_OFF_SQES);
    if (sqes_mapping == MAP_FAILED) {
        std::fprintf(stderr, "mmap SQEs: %s\n", std::strerror(errno));
        if (!single_map) {
            ::munmap(cq_ring, cq_ring_size);
        }
        ::munmap(
            sq_ring,
            single_map ? shared_size : sq_ring_size);
        ::close(ring_fd);
        return 13;
    }

    auto* const sq_tail = reinterpret_cast<unsigned*>(
        static_cast<char*>(sq_ring) + params.sq_off.tail);
    auto* const sq_array = reinterpret_cast<unsigned*>(
        static_cast<char*>(sq_ring) + params.sq_off.array);
    auto* const cq_head = reinterpret_cast<unsigned*>(
        static_cast<char*>(cq_ring) + params.cq_off.head);
    auto* const cq_tail = reinterpret_cast<unsigned*>(
        static_cast<char*>(cq_ring) + params.cq_off.tail);
    auto* const cqes = reinterpret_cast<io_uring_cqe*>(
        static_cast<char*>(cq_ring) + params.cq_off.cqes);
    auto* const sqes = static_cast<io_uring_sqe*>(sqes_mapping);

    sqes[0] = {};
    sqes[0].opcode = IORING_OP_NOP;
    sqes[0].user_data = 0x4343464f524745ULL;
    sq_array[*sq_tail & (params.sq_entries - 1U)] = 0U;
    std::atomic_thread_fence(std::memory_order_release);
    ++*sq_tail;

    const int entered = static_cast<int>(::syscall(
        __NR_io_uring_enter,
        ring_fd,
        1U,
        1U,
        IORING_ENTER_GETEVENTS,
        nullptr,
        0U));
    int result = 0;
    if (entered < 0) {
        const int error = errno;
        std::fprintf(
            stderr,
            "io_uring_enter: %s (%d)\n",
            std::strerror(error),
            error);
        result = unavailable_error(error) ? unavailable_exit_code : 14;
    } else {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (*cq_head == *cq_tail) {
            std::fprintf(stderr, "completion queue empty\n");
            result = 15;
        } else {
            const auto& cqe =
                cqes[*cq_head & (params.cq_entries - 1U)];
            std::printf(
                "ok features=0x%x sq=%u cq=%u cqe=%d user_data=0x%llx\n",
                params.features,
                params.sq_entries,
                params.cq_entries,
                cqe.res,
                static_cast<unsigned long long>(cqe.user_data));
            result = cqe.res == 0 ? 0 : 16;
            ++*cq_head;
        }
    }

    ::munmap(sqes_mapping, sqes_size);
    if (!single_map) {
        ::munmap(cq_ring, cq_ring_size);
    }
    ::munmap(
        sq_ring,
        single_map ? shared_size : sq_ring_size);
    ::close(ring_fd);
    return result;
}

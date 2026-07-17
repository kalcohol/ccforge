#pragma once

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <new>

namespace forge_test {

class counting_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] auto allocations() const noexcept -> std::size_t {
        return allocations_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto deallocations() const noexcept -> std::size_t {
        return deallocations_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto outstanding() const noexcept -> std::size_t {
        return allocations() - deallocations();
    }

private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        allocations_.fetch_add(1, std::memory_order_relaxed);
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* p,
        std::size_t bytes,
        std::size_t alignment) override {
        deallocations_.fetch_add(1, std::memory_order_relaxed);
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::atomic<std::size_t> allocations_{0};
    std::atomic<std::size_t> deallocations_{0};
};

class fail_next_resource final : public std::pmr::memory_resource {
public:
    void fail_next_allocation() noexcept {
        fail_next_.store(true, std::memory_order_release);
    }

private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        if (fail_next_.exchange(false, std::memory_order_acq_rel)) {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* p,
        std::size_t bytes,
        std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::atomic<bool> fail_next_{false};
};

} // namespace forge_test

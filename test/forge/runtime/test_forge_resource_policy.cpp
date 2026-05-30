#include <gtest/gtest.h>
#include <forge/resource_policy.hpp>
#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <vector>

namespace {

class counting_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] auto allocations() const noexcept -> std::size_t {
        return allocations_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto deallocations() const noexcept -> std::size_t {
        return deallocations_.load(std::memory_order_relaxed);
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

} // namespace

TEST(ForgeResourcePolicyTest, DefaultMemoryResourceMatchesPmrDefault) {
    EXPECT_EQ(forge::default_memory_resource(), std::pmr::get_default_resource());

    forge::resource_policy policy;
    EXPECT_EQ(policy.memory, std::pmr::get_default_resource());
}

TEST(ForgeResourcePolicyTest, NullMemoryResourceNormalizesToDefault) {
    EXPECT_EQ(
        forge::normalize_memory_resource(nullptr),
        std::pmr::get_default_resource());
}

TEST(ForgeResourcePolicyTest, CustomMemoryResourceIsObservable) {
    counting_resource resource;

    {
        std::pmr::vector<int> values{forge::normalize_memory_resource(&resource)};
        values.reserve(16);
        values.push_back(42);
        EXPECT_EQ(values.front(), 42);
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

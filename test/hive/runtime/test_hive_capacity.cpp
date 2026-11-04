#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {

template<class T>
struct context_bound_allocator : std::allocator<T> {
    using value_type = T;
    context_bound_allocator() = default;
    template<class U> constexpr context_bound_allocator(const context_bound_allocator<U>&) noexcept {}
    constexpr std::size_t max_size() const noexcept {
        return std::is_constant_evaluated() ? 5 : 17;
    }
    template<class U> bool operator==(const context_bound_allocator<U>&) const noexcept { return true; }
};

TEST(HiveCapacity, StaticAllocatorClampIsNotLostDuringSeparation) {
    using hive_type = std::hive<int, context_bound_allocator<int>>;
    static_assert(hive_type::block_capacity_hard_limits().max == 5);
    hive_type hive(std::hive_limits{1, 1});
    EXPECT_EQ(hive.get_allocator().max_size(), 17u);
    EXPECT_EQ(hive.max_size(), 5u);
    for (int value = 0; value != 5; ++value) hive.emplace(value);
    EXPECT_THROW(hive.emplace(5), std::length_error);
}

TEST(HiveCapacity, SelectedBlockMaximumDoesNotLimitTotalPopulation) {
    std::hive<int> hive(std::hive_limits{2, 2});
    const auto maximum = hive.max_size();
    EXPECT_GT(maximum, hive.block_capacity_limits().max);
    for (int value = 0; value != 17; ++value) hive.emplace(value);
    EXPECT_EQ(hive.size(), 17u);
    EXPECT_GE(hive.capacity(), hive.size());
    EXPECT_EQ(hive.max_size(), maximum);
}

TEST(HiveCapacity, InstanceLimitAppliesAcrossMultipleBlocks) {
    using namespace forge_hive_allocator_test;
    auto state = std::make_shared<allocation_state>();
    state->artificial_int_max_size = 11;
    {
        std::hive<int, stateful_allocator<int>> hive(
            std::hive_limits{1, 3}, stateful_allocator<int>(state, 1));
        EXPECT_EQ(hive.max_size(), 11u);
        for (int value = 0; value != 11; ++value) hive.emplace(value);
        EXPECT_EQ(hive.size(), 11u);
        EXPECT_EQ(hive.capacity(), 11u);
        const auto allocations = state->allocation_attempts;
        EXPECT_THROW(hive.emplace(11), std::length_error);
        EXPECT_THROW(hive.reserve(12), std::length_error);
        EXPECT_EQ(state->allocation_attempts, allocations);
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;

template<class T, class Allocator>
std::vector<T> values(const std::hive<T, Allocator>& hive) {
    return {hive.begin(), hive.end()};
}

TEST(HiveAllocator, SelectOnContainerCopyConstructionAndExplicitAllocator) {
    using allocator_type = stateful_allocator<int>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 2, 3, 4};
    {
        const allocator_type allocator(state, 7);
        std::hive<int, allocator_type> source(std::from_range, input, allocator);
        std::hive<int, allocator_type> copy(source);
        EXPECT_EQ(copy.get_allocator().id(), 107);
        EXPECT_EQ(source.get_allocator().id(), 7);
        EXPECT_EQ(values(copy), input);

        std::hive<int, allocator_type> explicit_copy(source, allocator_type(state, 22));
        EXPECT_EQ(explicit_copy.get_allocator().id(), 22);
        EXPECT_EQ(values(explicit_copy), input);
    }
    EXPECT_TRUE(state->clean());
}

template<bool Propagates>
void check_copy_assignment_propagation() {
    using allocator_type = stateful_allocator<int, Propagates>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{3, 1, 4, 1, 5};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, allocator_type(state, 11));
        std::hive<int, allocator_type> destination(
            std::from_range, std::vector<int>{8, 8}, allocator_type(state, 22));
        destination = source;
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.get_allocator().id(), Propagates ? 11 : 22);
        EXPECT_EQ(source.get_allocator().id(), 11);
        EXPECT_TRUE(state->valid_deallocations);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PoccaFalseKeepsDestinationAllocator) {
    check_copy_assignment_propagation<false>();
}

TEST(HiveAllocator, PoccaTrueReleasesWithOldAllocatorThenPropagates) {
    check_copy_assignment_propagation<true>();
}

TEST(HiveAllocator, PocmaTrueStealsAndPropagates) {
    using allocator_type = stateful_allocator<int, false, true>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 2, 3, 4};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::from_range, std::vector<int>{9, 9}, allocator_type(state, 20));
        auto migrated = std::next(source.begin());
        const int* address = std::addressof(*migrated);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 10);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(std::addressof(*migrated), address);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocmaFalseUnequalAllocatorUsesElementMovePath) {
    using allocator_type = stateful_allocator<int, false, false>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{2, 4, 6, 8, 10};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    const std::hive_limits destination_limits{defaults.min, defaults.max};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            destination_limits, allocator_type(state, 20));
        destination.emplace(-1);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.block_capacity_limits().min, destination_limits.min);
        EXPECT_EQ(destination.block_capacity_limits().max, destination_limits.max);
        EXPECT_GT(state->count_for_id(20), 0u);
        destination.emplace(12); // The unequal-move destination remains reusable.
        source.emplace(99);     // The moved-from source remains reusable.
        EXPECT_EQ(*source.begin(), 99);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocmaFalseEqualAllocatorStealsAndCopiesLimits) {
    using allocator_type = stateful_allocator<int, false, false>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{2, 3, 5, 7};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::hive_limits{defaults.min, defaults.max}, allocator_type(state, 10));
        auto migrated = source.begin();
        const int* address = std::addressof(*migrated);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 10);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(destination.block_capacity_limits().max, source_limits.max);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, AlwaysEqualPermitsStealWithoutPocma) {
    using allocator_type = stateful_allocator<int, false, false, false, true>;
    static_assert(std::allocator_traits<allocator_type>::is_always_equal::value);
    static_assert(noexcept(
        std::declval<std::hive<int, allocator_type>&>() =
            std::declval<std::hive<int, allocator_type>&&>()));
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{4, 5, 6};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(allocator_type(state, 20));
        auto migrated = source.begin();
        const int* address = std::addressof(*migrated);
        ASSERT_EQ(source.get_allocator(), destination.get_allocator());
        destination = std::move(source);
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(destination.block_capacity_limits().max, source_limits.max);
        EXPECT_EQ(values(destination), input);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, UnequalAllocatorMoveConstructorMovesElementsAndCopiesLimits) {
    using allocator_type = stateful_allocator<int>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 3, 5, 7};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::move(source), allocator_type(state, 20));
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(values(destination), input);
        // The unequal-allocator move constructor moves elements; unlike the
        // steal path, the standard does not require copying current-limits.
        destination.emplace(9);
        source.emplace(11);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocsTrueSwapsUnequalStatefulAllocators) {
    using allocator_type = stateful_allocator<int, false, false, true>;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<int, allocator_type> left(
            std::from_range, std::vector<int>{1, 2}, allocator_type(state, 10));
        std::hive<int, allocator_type> right(
            std::from_range, std::vector<int>{7, 8, 9}, allocator_type(state, 20));
        auto migrated = right.begin();
        const int* address = std::addressof(*migrated);
        left.swap(right);
        EXPECT_EQ(left.get_allocator().id(), 20);
        EXPECT_EQ(right.get_allocator().id(), 10);
        EXPECT_EQ(left.get_iterator(address), migrated);
        EXPECT_EQ(values(left), (std::vector<int>{7, 8, 9}));
        EXPECT_EQ(values(right), (std::vector<int>{1, 2}));
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocsFalseEqualAllocatorSwapKeepsAllocatorAndAddresses) {
    using allocator_type = stateful_allocator<int, false, false, false>;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<int, allocator_type> left(
            std::from_range, std::vector<int>{1, 2}, allocator_type(state, 10));
        std::hive<int, allocator_type> right(
            std::from_range, std::vector<int>{7, 8, 9}, allocator_type(state, 10));
        auto migrated = right.begin();
        const int* address = std::addressof(*migrated);
        left.swap(right);
        EXPECT_EQ(left.get_allocator().id(), 10);
        EXPECT_EQ(right.get_allocator().id(), 10);
        EXPECT_EQ(left.get_iterator(address), migrated);
        EXPECT_EQ(values(left), (std::vector<int>{7, 8, 9}));
        EXPECT_EQ(values(right), (std::vector<int>{1, 2}));
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

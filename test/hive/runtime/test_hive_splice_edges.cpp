#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;
using allocator_type = stateful_allocator<int>;
using hive_type = std::hive<int, allocator_type>;

struct hive_snapshot {
    std::vector<int> order;
    std::vector<const int*> addresses;
    std::size_t capacity;
};

hive_snapshot snapshot(const hive_type& hive) {
    hive_snapshot result;
    result.capacity = hive.capacity();
    for (const auto& value : hive) {
        result.order.push_back(value);
        result.addresses.push_back(std::addressof(value));
    }
    return result;
}

void expect_snapshot(const hive_type& hive, const hive_snapshot& expected) {
    ASSERT_EQ(hive.size(), expected.order.size());
    EXPECT_EQ(hive.capacity(), expected.capacity);
    std::size_t index = 0;
    for (const auto& value : hive) {
        ASSERT_LT(index, expected.order.size());
        EXPECT_EQ(value, expected.order[index]);
        EXPECT_EQ(std::addressof(value), expected.addresses[index]);
        ++index;
    }
    EXPECT_EQ(index, expected.order.size());
}

std::vector<int> sorted(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values;
}

TEST(HiveSpliceEdges, SmallMaxSizeDoesNotRejectAndSourceReservedBlocksAreNotTransferred) {
    auto state = std::make_shared<allocation_state>();
    {
        constexpr auto defaults = hive_type::block_capacity_default_limits();
        const std::hive_limits limits{defaults.min, defaults.min};
        std::vector<int> source_values;
        for (int value = 0; value < 40; ++value) source_values.push_back(value);
        hive_type source(std::from_range, source_values, limits, allocator_type(state, 7));
        hive_type destination(
            std::from_range, std::vector<int>{90, 91}, limits, allocator_type(state, 7));
        ASSERT_EQ(source.get_allocator(), destination.get_allocator());

        source.trim_capacity(); // Remove any proactive reserved capacity first.
        const auto source_active_capacity = source.capacity();
        const auto destination_capacity_before = destination.capacity();
        source.reserve(source_active_capacity + 3 * defaults.min);
        ASSERT_GT(source.capacity(), source_active_capacity);
        const auto source_reserved_capacity = source.capacity() - source_active_capacity;
        const auto destination_size = destination.size();
        const auto source_size = source.size();
        auto expected_values = source_values;
        expected_values.push_back(90);
        expected_values.push_back(91);

        auto migrated = std::next(source.begin());
        auto migrated_previous = std::prev(migrated);
        auto migrated_next = std::next(migrated);
        const int* const migrated_address = std::addressof(*migrated);
        auto destination_iterator = destination.begin();
        const int* const destination_address = std::addressof(*destination_iterator);

        // Both containers now report less than one existing active block.  This
        // is not a splice failure condition in [hive.operations].
        state->artificial_int_max_size = 0;
        ASSERT_EQ(destination.max_size(), 0u);
        ASSERT_GT(source.capacity(), destination.max_size());
        destination.splice(source);

        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.size(), destination_size + source_size);
        EXPECT_EQ(sorted(std::vector<int>(destination.begin(), destination.end())),
                  sorted(expected_values));
        // Nonempty destination forces the general path.  Destination gains
        // exactly the publicly measured active capacity, never source reserved
        // capacity.  Source may retain or release its now-unused reserved blocks.
        EXPECT_EQ(destination.capacity(), destination_capacity_before + source_active_capacity);
        EXPECT_LE(source.capacity(), source_reserved_capacity);
        EXPECT_EQ(destination.get_iterator(migrated_address), migrated);
        EXPECT_EQ(destination.get_iterator(destination_address), destination_iterator);
        auto after_previous = migrated_previous;
        auto before_next = migrated_next;
        EXPECT_EQ(++after_previous, migrated);
        EXPECT_EQ(--before_next, migrated);
        EXPECT_EQ((migrated_previous <=> migrated), std::strong_ordering::less);
        EXPECT_EQ((migrated_next <=> migrated), std::strong_ordering::greater);

        source.trim_capacity();
        EXPECT_EQ(source.capacity(), 0u); // Everything left in source was reserved.
        EXPECT_EQ(destination.get_iterator(migrated_address), migrated);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveSpliceEdges, EmptyDestinationWithReservedStorageUsesGeneralPath) {
    auto state = std::make_shared<allocation_state>();
    {
        constexpr auto defaults = hive_type::block_capacity_default_limits();
        const std::hive_limits limits{defaults.min, defaults.min};
        hive_type destination(limits, allocator_type(state, 9));
        destination.reserve(3 * defaults.min);
        ASSERT_TRUE(destination.empty());
        ASSERT_GT(destination.capacity(), 0u);

        std::vector<int> input;
        for (int value = 0; value < 20; ++value) input.push_back(value);
        hive_type source(std::from_range, input, limits, allocator_type(state, 9));
        source.trim_capacity(); // Ensure this source has active blocks only.
        auto migrated = std::next(source.begin(), 2);
        auto previous = std::prev(migrated);
        auto next = std::next(migrated);
        const int* const address = std::addressof(*migrated);
        const auto combined_capacity = source.capacity() + destination.capacity();

        destination.splice(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(source.capacity(), 0u); // This source had no reserved blocks.
        EXPECT_EQ(destination.size(), input.size());
        EXPECT_EQ(destination.capacity(), combined_capacity);
        EXPECT_GE(destination.capacity(), 3 * defaults.min);
        EXPECT_EQ(sorted(std::vector<int>(destination.begin(), destination.end())), input);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        auto after_previous = previous;
        auto before_next = next;
        EXPECT_EQ(++after_previous, migrated);
        EXPECT_EQ(--before_next, migrated);
        EXPECT_EQ((previous <=> migrated), std::strong_ordering::less);
        EXPECT_EQ((next <=> migrated), std::strong_ordering::greater);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveSpliceEdges, IncompatibleBlockLimitsThrowWithBothContainersUnchanged) {
    auto state = std::make_shared<allocation_state>();
    {
        hive_type source(allocator_type(state, 11));
        source.emplace(42);
        source.trim_capacity(); // Exactly one active block and no reserved blocks.
        const auto source_block_capacity = source.capacity();
        ASSERT_GT(source_block_capacity, 0u);
        constexpr auto hard = hive_type::block_capacity_hard_limits();

        // Derive valid target limits which exclude the actual public source
        // block capacity, without assuming a particular block size.
        std::hive_limits target_limits = hard;
        if (hard.min < source_block_capacity) {
            target_limits = std::hive_limits{hard.min, source_block_capacity - 1};
        } else {
            ASSERT_LT(source_block_capacity, hard.max);
            target_limits = std::hive_limits{source_block_capacity + 1, hard.max};
        }
        ASSERT_TRUE(hive_type::is_within_hard_limits(target_limits));

        hive_type destination(
            std::from_range, std::vector<int>{80, 81}, target_limits,
            allocator_type(state, 11));
        const auto source_before = snapshot(source);
        const auto destination_before = snapshot(destination);
        auto source_iterator = source.begin();
        auto destination_iterator = destination.begin();
        const int* const source_address = std::addressof(*source_iterator);
        const int* const destination_address = std::addressof(*destination_iterator);

        EXPECT_THROW(destination.splice(source), std::length_error);
        expect_snapshot(source, source_before);
        expect_snapshot(destination, destination_before);
        EXPECT_EQ(source.get_iterator(source_address), source_iterator);
        EXPECT_EQ(destination.get_iterator(destination_address), destination_iterator);
        EXPECT_EQ(destination.block_capacity_limits().min, target_limits.min);
        EXPECT_EQ(destination.block_capacity_limits().max, target_limits.max);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveSpliceEdges, DestinationControlAllocationFailureHasNoEffects) {
    auto state = std::make_shared<allocation_state>();
    {
        hive_type source(
            std::from_range, std::vector<int>{1, 2, 3, 4}, allocator_type(state, 12));
        source.trim_capacity();
        const auto active_capacity = source.capacity();
        source.reserve(active_capacity +
                       hive_type::block_capacity_default_limits().min);
        ASSERT_GT(source.capacity(), active_capacity); // Forces the general path.
        hive_type destination(allocator_type(state, 12));
        ASSERT_EQ(destination.capacity(), 0u);
        const auto source_before = snapshot(source);
        const auto destination_before = snapshot(destination);
        auto migrated = std::next(source.begin());
        const int* const address = std::addressof(*migrated);

        state->fail_after = 0;
        EXPECT_THROW(destination.splice(source), std::bad_alloc);
        state->fail_after = -1;
        expect_snapshot(source, source_before);
        expect_snapshot(destination, destination_before);
        EXPECT_EQ(source.get_iterator(address), migrated);
        EXPECT_EQ(destination.block_capacity_limits().min,
                  hive_type::block_capacity_default_limits().min);

        destination.splice(source); // The failed control allocation is retryable.
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_iterator(address), migrated);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveSpliceEdges, EmptySourceAndSelfSplicePreserveObservableContents) {
    auto state = std::make_shared<allocation_state>();
    {
        hive_type destination(
            std::from_range, std::vector<int>{1, 2, 3}, allocator_type(state, 13));
        hive_type empty_source(allocator_type(state, 13));
        empty_source.trim_capacity();
        empty_source.reserve(empty_source.capacity() +
                             2 * hive_type::block_capacity_default_limits().min);
        ASSERT_TRUE(empty_source.empty());
        ASSERT_GT(empty_source.capacity(), 0u);
        const auto destination_before = snapshot(destination);
        const auto empty_capacity = empty_source.capacity();
        auto destination_iterator = destination.begin();
        const int* const destination_address = std::addressof(*destination_iterator);

        destination.splice(empty_source);
        expect_snapshot(destination, destination_before);
        EXPECT_TRUE(empty_source.empty());
        EXPECT_EQ(empty_source.capacity(), empty_capacity);
        EXPECT_EQ(destination.get_iterator(destination_address), destination_iterator);

        const auto before_self = snapshot(destination);
        destination.splice(destination); // Erroneous self-splice has no effects.
        expect_snapshot(destination, before_self);
        EXPECT_EQ(destination.get_iterator(destination_address), destination_iterator);
        EXPECT_EQ(destination.get_allocator().id(), 13);
        // No saved end iterator is read after either non-self or self splice.
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

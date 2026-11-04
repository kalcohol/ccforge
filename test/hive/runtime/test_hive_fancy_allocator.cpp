#include <gtest/gtest.h>

#include <hive>

#include "hive_test_fancy_allocator.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_fancy_allocator_test::fancy_allocator;
using forge_hive_fancy_allocator_test::fancy_ptr;

TEST(HiveFancyAllocator, VoidPointerRoundTripsPreserveAllocationHandle) {
    auto state = std::make_shared<allocation_state>();
    fancy_allocator<int> allocator(state, 7);
    auto allocated = allocator.allocate(1);
    fancy_ptr<void> erased = allocated;
    auto recovered = static_cast<fancy_ptr<int>>(erased);
    fancy_ptr<const int> const_allocated = allocated;
    fancy_ptr<const void> const_erased = const_allocated;
    auto const_recovered = static_cast<fancy_ptr<const int>>(const_erased);
    auto const_from_mutable = static_cast<fancy_ptr<const int>>(erased);
    EXPECT_EQ(std::to_address(recovered), std::to_address(allocated));
    EXPECT_EQ(std::to_address(const_recovered), std::to_address(allocated));
    EXPECT_EQ(std::to_address(const_from_mutable), std::to_address(allocated));
    allocator.deallocate(recovered, 1);
    EXPECT_TRUE(state->clean());

    fancy_ptr<void> null_void;
    fancy_ptr<const void> null_const_void;
    EXPECT_EQ(std::to_address(static_cast<fancy_ptr<int>>(null_void)), nullptr);
    EXPECT_EQ(std::to_address(static_cast<fancy_ptr<const int>>(null_const_void)), nullptr);
}

TEST(HiveFancyAllocator, FullLifecycleIterationOwnershipAndSort) {
    using allocator_type = fancy_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    using const_pointer = typename hive_type::const_pointer;
    static_assert(std::is_same_v<const_pointer, fancy_ptr<const int>>);
    static_assert(!std::is_same_v<typename hive_type::pointer, int*>);

    auto state = std::make_shared<allocation_state>();
    {
        hive_type original(allocator_type(state, 1));
        for (int value = 0; value < 96; ++value) original.emplace(value);
        for (auto it = original.begin(); it != original.end();) {
            if (*it % 4 == 0) it = original.erase(it);
            else ++it;
        }
        for (int value = 96; value < 128; ++value) original.emplace(value);
        ASSERT_EQ(original.size(), 104u);

        std::set<const int*> addresses;
        std::size_t visited = 0;
        for (auto it = original.begin(); it != original.end(); ++it, ++visited) {
            EXPECT_TRUE(addresses.insert(std::addressof(*it)).second);
        }
        EXPECT_EQ(visited, original.size());
        EXPECT_EQ(static_cast<std::size_t>(std::distance(original.rbegin(), original.rend())),
                  original.size());

        auto migrated = std::next(original.begin(), 3);
        auto migrated_previous = std::prev(migrated);
        auto migrated_next = std::next(migrated);
        const int* const migrated_address = std::addressof(*migrated);
        const_pointer pointer = std::pointer_traits<const_pointer>::pointer_to(
            std::as_const(*migrated));
        ASSERT_EQ(std::to_address(pointer), migrated_address);
        EXPECT_EQ(original.get_iterator(pointer), migrated);
        const auto& const_original = original;
        EXPECT_EQ(const_original.get_iterator(pointer),
                  typename hive_type::const_iterator(migrated));

        hive_type moved(std::move(original));
        EXPECT_TRUE(original.empty());
        EXPECT_EQ(moved.get_iterator(pointer), migrated);
        EXPECT_EQ(std::addressof(*migrated), migrated_address);

        hive_type other(allocator_type(state, 2));
        other.emplace(1000);
        other.emplace(1001);
        auto other_iterator = other.begin();
        const_pointer other_pointer = std::pointer_traits<const_pointer>::pointer_to(
            std::as_const(*other_iterator));
        moved.swap(other); // POCS=true permits unequal stateful allocators.
        EXPECT_EQ(moved.get_allocator().id(), 2);
        EXPECT_EQ(other.get_allocator().id(), 1);
        EXPECT_EQ(other.get_iterator(pointer), migrated);
        EXPECT_EQ(moved.get_iterator(other_pointer), other_iterator);
        EXPECT_EQ(std::addressof(*migrated), migrated_address);

        // Iterators migrated by move and swap retain traversal and ordering in
        // their new owner.  No old end iterator is used after either operation.
        auto after_previous = migrated_previous;
        auto before_next = migrated_next;
        EXPECT_EQ(++after_previous, migrated);
        EXPECT_EQ(--before_next, migrated);
        EXPECT_LT(migrated_previous, migrated);
        EXPECT_GT(migrated_next, migrated);
        EXPECT_EQ((migrated_previous <=> migrated), std::strong_ordering::less);
        EXPECT_EQ((migrated_next <=> migrated), std::strong_ordering::greater);

        hive_type source(allocator_type(state, 1));
        for (int value = 2000; value < 2032; ++value) source.emplace(value);
        auto source_iterator = std::next(source.begin(), 4);
        auto source_previous = std::prev(source_iterator);
        auto source_next = std::next(source_iterator);
        const_pointer source_pointer = std::pointer_traits<const_pointer>::pointer_to(
            std::as_const(*source_iterator));
        ASSERT_EQ(source.get_allocator(), other.get_allocator());
        other.splice(source); // Nonempty destination, equal allocator.
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(other.get_iterator(source_pointer), source_iterator);
        EXPECT_EQ(other.get_iterator(pointer), migrated);
        auto source_after_previous = source_previous;
        auto source_before_next = source_next;
        EXPECT_EQ(++source_after_previous, source_iterator);
        EXPECT_EQ(--source_before_next, source_iterator);
        EXPECT_EQ((source_previous <=> source_iterator), std::strong_ordering::less);
        EXPECT_EQ((source_next <=> source_iterator), std::strong_ordering::greater);

        const auto int_allocations_before_sort = state->allocation_attempts_for<int>();
        other.sort(); // Heap sort builds a position table via allocator rebind (no temporary T vector).
        EXPECT_TRUE(std::is_sorted(other.begin(), other.end()));
        EXPECT_EQ(other.size(), 136u);
        if (state->allocation_attempts_for<int>() > int_allocations_before_sort) {
            EXPECT_EQ(state->current_allocations_for<int>(), 0u);
        }

        // Hive metadata/storage comes through non-int rebinds.  Every recorded
        // allocation still carries its exact type, size, alignment and owner.
        EXPECT_GT(state->allocation_attempts,
                  state->allocation_attempts_for<int>());
        EXPECT_TRUE(state->valid_deallocations);
        EXPECT_EQ(moved.size(), 2u);
        EXPECT_EQ(std::set<int>(moved.begin(), moved.end()), (std::set<int>{1000, 1001}));
    }
    EXPECT_TRUE(state->clean());
    EXPECT_EQ(state->current_allocations_for<int>(), 0u);
}

} // namespace

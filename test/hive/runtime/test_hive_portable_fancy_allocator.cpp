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

} // namespace

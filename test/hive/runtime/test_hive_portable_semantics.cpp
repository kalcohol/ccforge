#include <gtest/gtest.h>

#include <hive>
#include <ranges>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

template<class T, class Allocator>
std::vector<T> values(const std::hive<T, Allocator>& hive) {
    return {hive.begin(), hive.end()};
}

template<class T>
std::vector<T> sorted(std::vector<T> input) {
    std::sort(input.begin(), input.end());
    return input;
}

template<class Allocator>
auto make_ordered_hive(const std::vector<int>& input, const Allocator& allocator) {
    return std::hive<int, Allocator>(std::from_range, input, allocator);
}

TEST(HiveCoreSemantics, EndObserversAreUsedOnlyWithinTheirValidityRules) {
    std::hive<int> hive{1, 2, 3, 4};
    auto stable_end = hive.end();
    const int* const first_address = std::addressof(*hive.begin());

    hive.reserve(hive.capacity() + 32);
    EXPECT_EQ(stable_end, hive.end()); // reserve preserves elements and end.
    EXPECT_EQ(std::addressof(*hive.begin()), first_address);
    hive.trim_capacity();
    EXPECT_EQ(stable_end, hive.end()); // trim also preserves end.

    auto second = std::next(hive.begin());
    auto returned = hive.erase(second); // Not the last sequence element.
    EXPECT_EQ(*returned, 3);
    EXPECT_EQ(stable_end, hive.end());

    hive.assign_range(std::vector<int>{7, 6});
    EXPECT_EQ(stable_end, hive.end()); // hive assign has no blanket end invalidation.
    EXPECT_EQ(values(hive), (std::vector<int>{7, 6}));

    // emplace invalidates end.  Deliberately do not read or compare the old
    // insertion-era end after this statement.
    hive.emplace(5);
    EXPECT_EQ(hive.size(), 3u);

    auto last = hive.end();
    --last;
    returned = hive.erase(last);
    // The returned iterator is the new end; the pre-erase end is not reused.
    EXPECT_EQ(returned, hive.end());
    EXPECT_EQ(hive.size(), 2u);

    hive.clear();
    EXPECT_EQ(hive.begin(), hive.end());
    EXPECT_TRUE(hive.empty());
}

TEST(HiveCopyMoveAssign, ConstructorsAssignmentsOrderLimitsAndIteratorMigration) {
    constexpr auto defaults = std::hive<int>::block_capacity_default_limits();
    const std::hive_limits limits{defaults.min, defaults.min};
    const std::vector<int> input{1, 2, 3, 4, 5, 6};
    std::hive<int> source(std::from_range, input, limits);
    ASSERT_EQ(values(source), input);

    std::hive<int> copied(source);
    EXPECT_EQ(values(copied), input);
    EXPECT_EQ(copied.block_capacity_limits().min, limits.min);
    EXPECT_EQ(copied.block_capacity_limits().max, limits.max);

    std::hive<int> copy_with_allocator(source, std::allocator<int>{});
    EXPECT_EQ(values(copy_with_allocator), input);

    auto migrated = std::next(source.begin(), 2);
    const int* migrated_address = std::addressof(*migrated);
    std::hive<int> moved(std::move(source));
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(values(moved), input);
    EXPECT_EQ(*migrated, 3);
    EXPECT_EQ(std::addressof(*migrated), migrated_address);
    EXPECT_EQ(moved.get_iterator(migrated_address), migrated);
    EXPECT_EQ(moved.block_capacity_limits().min, limits.min);

    std::hive<int> move_with_allocator(std::move(moved), std::allocator<int>{});
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(values(move_with_allocator), input);

    const std::hive_limits destination_limits{defaults.min, defaults.max};
    std::hive<int> copy_assigned(destination_limits);
    copy_assigned.emplace(99);
    copy_assigned = move_with_allocator;
    EXPECT_EQ(values(copy_assigned), input);
    EXPECT_EQ(copy_assigned.block_capacity_limits().min, destination_limits.min);
    EXPECT_EQ(copy_assigned.block_capacity_limits().max, destination_limits.max);

    copy_assigned = copy_assigned;
    EXPECT_EQ(values(copy_assigned), input);

    auto moving_iterator = std::next(move_with_allocator.begin());
    const int* moving_address = std::addressof(*moving_iterator);
    std::hive<int> move_assigned;
    move_assigned.emplace(-1);
    move_assigned = std::move(move_with_allocator);
    EXPECT_TRUE(move_with_allocator.empty());
    EXPECT_EQ(values(move_assigned), input);
    EXPECT_EQ(std::addressof(*moving_iterator), moving_address);
    EXPECT_EQ(move_assigned.get_iterator(moving_address), moving_iterator);
    EXPECT_EQ(move_assigned.block_capacity_limits().min, limits.min);

    move_assigned = {9, 8, 7};
    EXPECT_EQ(values(move_assigned), (std::vector<int>{9, 8, 7}));
    move_assigned.assign(4, 11);
    EXPECT_EQ(values(move_assigned), (std::vector<int>{11, 11, 11, 11}));
    move_assigned.assign({2, 4, 6});
    EXPECT_EQ(values(move_assigned), (std::vector<int>{2, 4, 6}));
    const std::vector<int> replacement{5, 3, 1};
    move_assigned.assign(replacement.begin(), replacement.end());
    EXPECT_EQ(values(move_assigned), replacement);
    move_assigned.assign_range(std::vector<int>{7, 0, 7});
    EXPECT_EQ(values(move_assigned), (std::vector<int>{7, 0, 7}));

    std::hive<int> reuse(std::move(move_assigned));
    EXPECT_TRUE(move_assigned.empty());
    move_assigned.emplace(123);
    EXPECT_EQ(move_assigned.size(), 1u);
    EXPECT_EQ(*move_assigned.begin(), 123);
}

TEST(HiveOperations, SplicePreservesAddressAndTransfersIteratorOwnership) {
    const std::vector<int> left_input{1, 2};
    const std::vector<int> right_input{3, 4, 5};
    std::hive<int> left(std::from_range, left_input);
    std::hive<int> right(std::from_range, right_input);
    auto migrated = std::next(right.begin());
    const int* migrated_address = std::addressof(*migrated);
    std::map<int, const int*> addresses;
    for (auto& hive : {std::ref(left), std::ref(right)}) {
        for (auto it = hive.get().begin(); it != hive.get().end(); ++it) {
            addresses.emplace(*it, std::addressof(*it));
        }
    }

    left.splice(right);
    EXPECT_TRUE(right.empty());
    EXPECT_EQ(left.size(), 5u);
    EXPECT_EQ(sorted(values(left)), (std::vector<int>{1, 2, 3, 4, 5}));
    EXPECT_EQ(*migrated, 4);
    EXPECT_EQ(std::addressof(*migrated), migrated_address);
    EXPECT_EQ(left.get_iterator(migrated_address), migrated);
    for (auto it = left.begin(); it != left.end(); ++it) {
        EXPECT_EQ(std::addressof(*it), addresses.at(*it));
    }

    std::hive<int> rvalue_target;
    auto another = left.begin();
    const int* another_address = std::addressof(*another);
    rvalue_target.splice(std::move(left));
    EXPECT_TRUE(left.empty());
    EXPECT_EQ(rvalue_target.size(), 5u);
    EXPECT_EQ(rvalue_target.get_iterator(another_address), another);
}

TEST(HiveOperations, SortAndGetIteratorConstOverloads) {
    std::hive<int> hive{4, 1, 3, 1, 2, 5, 0};
    int comparisons = 0;
    hive.sort([&](int left, int right) {
        ++comparisons;
        return left < right;
    });
    EXPECT_EQ(values(hive), (std::vector<int>{0, 1, 1, 2, 3, 4, 5}));
    EXPECT_GT(comparisons, 0);

    auto it = std::next(hive.begin(), 3);
    int* pointer = std::addressof(*it);
    EXPECT_EQ(hive.get_iterator(pointer), it);
    *hive.get_iterator(pointer) = 20;
    EXPECT_EQ(*it, 20);
    const auto& const_hive = hive;
    const int* const_pointer = std::addressof(*it);
    auto const_it = const_hive.get_iterator(const_pointer);
    static_assert(std::is_same_v<decltype(const_it), std::hive<int>::const_iterator>);
    EXPECT_EQ(const_it, it);
    EXPECT_EQ(*const_it, 20);

    hive.sort();
    EXPECT_TRUE(std::is_sorted(hive.begin(), hive.end()));
}

} // namespace

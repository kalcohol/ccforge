#include <gtest/gtest.h>

#include <hive>

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

TEST(HiveCoreSemantics, CrossBlockHolesReverseAndAddressStability) {
    std::hive<int> hive;
    for (int index = 0; index < 192; ++index) {
        auto inserted = hive.emplace(index);
        ASSERT_EQ(*inserted, index);
    }
    ASSERT_EQ(hive.size(), 192u);

    std::unordered_map<int, const int*> addresses;
    std::vector<int> initial_order;
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        addresses.emplace(*it, std::addressof(*it));
        initial_order.push_back(*it);
    }
    EXPECT_EQ(addresses.size(), hive.size());

    std::vector<int> expected;
    for (auto it = hive.begin(); it != hive.end();) {
        if (*it % 3 == 0) {
            auto successor = std::next(it);
            if (successor == hive.end()) {
                it = hive.erase(it);
                EXPECT_EQ(it, hive.end());
            } else {
                const int successor_value = *successor;
                auto returned = hive.erase(it);
                ASSERT_NE(returned, hive.end());
                EXPECT_EQ(*returned, successor_value);
                it = returned;
            }
        } else {
            expected.push_back(*it);
            ++it;
        }
    }

    EXPECT_EQ(hive.size(), expected.size());
    EXPECT_EQ(values(hive), expected);
    const std::vector<int> reversed(expected.rbegin(), expected.rend());
    EXPECT_EQ(std::vector<int>(hive.rbegin(), hive.rend()), reversed);
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        EXPECT_EQ(std::addressof(*it), addresses.at(*it));
        EXPECT_EQ(hive.get_iterator(std::addressof(*it)), it);
    }

    // Reuse all kinds of holes without prescribing which hole is selected.
    for (int index = 192; index < 288; ++index) {
        hive.emplace(index);
    }
    std::multiset<int> all_values;
    for (const int value : values(hive)) {
        all_values.insert(value);
    }
    EXPECT_EQ(all_values.size(), hive.size());
    for (const int value : expected) {
        EXPECT_EQ(all_values.count(value), 1u);
    }
    for (int value = 192; value < 288; ++value) {
        EXPECT_EQ(all_values.count(value), 1u);
    }
    for (const int value : expected) {
        auto it = hive.get_iterator(addresses.at(value));
        EXPECT_EQ(*it, value);
    }
}

TEST(HiveCoreSemantics, RangeEraseReturnsOriginalLastOrNewEnd) {
    std::vector<int> input;
    for (int index = 0; index < 40; ++index) input.push_back(index);
    std::hive<int> hive(std::from_range, input);

    auto first = std::next(hive.begin(), 5);
    auto last = std::next(hive.begin(), 11);
    const int successor = *last;
    auto returned = hive.erase(first, last);
    ASSERT_NE(returned, hive.end());
    EXPECT_EQ(*returned, successor);
    EXPECT_EQ(hive.size(), 34u);

    first = std::next(hive.begin(), 20);
    last = hive.end();
    returned = hive.erase(first, last);
    EXPECT_EQ(returned, hive.end());
    EXPECT_EQ(hive.size(), 20u);

    first = hive.begin();
    last = hive.begin();
    returned = hive.erase(first, last);
    EXPECT_EQ(returned, hive.begin());
    EXPECT_EQ(hive.size(), 20u);
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

struct self_value {
    int value;
    explicit self_value(int input) : value(input) {}
    self_value(const self_value& other) : value(other.value) {}
    self_value(self_value&& other) noexcept : value(other.value) {}
};

TEST(HiveCoreSemantics, SelfReferentialEmplaceAndInsertArguments) {
    std::hive<self_value> hive;
    auto original = hive.emplace(42);
    const int original_address_value = original->value;
    self_value& reference = *original;
    const self_value& const_reference = reference;
    const self_value* const original_address = std::addressof(reference);

    auto copy_emplaced = hive.emplace(const_reference);
    EXPECT_EQ(copy_emplaced->value, 42);
    auto copy_inserted = hive.insert(const_reference);
    EXPECT_EQ(copy_inserted->value, 42);
    auto move_emplaced = hive.emplace(std::move(reference));
    EXPECT_EQ(move_emplaced->value, 42);
    auto move_inserted = hive.insert(std::move(reference));
    EXPECT_EQ(move_inserted->value, 42);
    auto hinted = hive.emplace_hint(hive.begin(), const_reference);
    EXPECT_EQ(hinted->value, 42);

    EXPECT_EQ(hive.size(), 6u);
    EXPECT_EQ(std::addressof(*original), original_address);
    EXPECT_EQ(original_address_value, 42);
    for (const auto& item : hive) {
        EXPECT_EQ(item.value, 42);
    }
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

TEST(HiveOperations, UniquePredicateOrderCountAndResult) {
    const std::vector<int> input{1, 1, 2, 2, 2, 1, 3, 3};
    std::hive<int> hive(std::from_range, input);
    std::vector<std::pair<int, int>> calls;
    const auto removed = hive.unique([&](const int& current, const int& previous) {
        calls.emplace_back(current, previous);
        return current == previous;
    });
    EXPECT_EQ(removed, 4u);
    EXPECT_EQ(values(hive), (std::vector<int>{1, 2, 1, 3}));
    ASSERT_EQ(calls.size(), input.size() - 1);
    EXPECT_EQ(calls.front(), (std::pair<int, int>{1, 1}));
    EXPECT_EQ(calls.back(), (std::pair<int, int>{3, 3}));

    std::hive<int> defaulted(std::from_range, input);
    EXPECT_EQ(defaulted.unique(), 4u);
    EXPECT_EQ(values(defaulted), (std::vector<int>{1, 2, 1, 3}));

    std::hive<int> empty;
    int calls_for_empty = 0;
    EXPECT_EQ(empty.unique([&](int, int) {
        ++calls_for_empty;
        return true;
    }), 0u);
    EXPECT_EQ(calls_for_empty, 0);
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

struct lookup_key {
    int value;
};
struct keyed_value {
    int key;
    int payload;
};
bool operator==(const keyed_value& element, const lookup_key& key) {
    return element.key == key.value;
}

TEST(HiveOperations, SortUsesLogLinearComparisonBudget) {
    std::vector<int> input;
    for (int value = 127; value >= 0; --value) input.push_back(value);
    std::hive<int> hive(std::from_range, input);
    std::size_t comparisons = 0;
    hive.sort([&](int left, int right) {
        ++comparisons;
        return left < right;
    });
    EXPECT_TRUE(std::is_sorted(hive.begin(), hive.end()));

    std::size_t ceiling_log = 0;
    std::size_t power = 1;
    while (power < hive.size()) {
        power *= 2;
        ++ceiling_log;
    }
    const std::size_t n = hive.size();
    // Deliberately loose for an O(N log N) contract, while still rejecting a
    // quadratic comparison implementation at this input size.
    const std::size_t comparison_bound = 8 * n * ceiling_log + 8 * n + 64;
    EXPECT_GT(comparisons, 0u);
    EXPECT_LE(comparisons, comparison_bound);
}

TEST(HiveNonMember, EraseEraseIfAndSwap) {
    std::hive<keyed_value> hive;
    for (int index = 0; index < 12; ++index) {
        hive.emplace(keyed_value{index % 3, index});
    }
    const auto erased = std::erase(hive, lookup_key{1});
    EXPECT_EQ(erased, 4u);
    EXPECT_EQ(hive.size(), 8u);
    for (const auto& item : hive) EXPECT_NE(item.key, 1);

    const auto removed = std::erase_if(hive, [](const keyed_value& item) {
        return item.payload % 2 == 0;
    });
    EXPECT_EQ(removed, 4u);
    EXPECT_EQ(hive.size(), 4u);
    for (const auto& item : hive) EXPECT_EQ(item.payload % 2, 1);

    constexpr auto defaults = std::hive<int>::block_capacity_default_limits();
    std::hive<int> left(std::hive_limits{defaults.min, defaults.min});
    std::hive<int> right(std::hive_limits{defaults.min, defaults.max});
    left.assign_range(std::vector<int>{1, 2});
    right.assign_range(std::vector<int>{7, 8, 9});
    auto migrated = right.begin();
    const int* address = std::addressof(*migrated);
    const auto left_limits = left.block_capacity_limits();
    const auto right_limits = right.block_capacity_limits();
    left.swap(right);
    EXPECT_EQ(left.size(), 3u);
    EXPECT_EQ(right.size(), 2u);
    EXPECT_EQ(left.get_iterator(address), migrated);
    EXPECT_EQ(left.block_capacity_limits().min, right_limits.min);
    EXPECT_EQ(left.block_capacity_limits().max, right_limits.max);
    EXPECT_EQ(right.block_capacity_limits().min, left_limits.min);

    using std::swap;
    swap(left, right);
    EXPECT_EQ(right.get_iterator(address), migrated);
}

TEST(HiveCapacity, ReserveTrimShrinkReshapeAndLimits) {
    using hive_type = std::hive<int>;
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    static_assert(noexcept(hive_type::block_capacity_default_limits()));
    static_assert(noexcept(hive_type::block_capacity_hard_limits()));
    EXPECT_TRUE(hive_type::is_within_hard_limits(defaults));
    EXPECT_LE(hard.min, defaults.min);
    EXPECT_LE(defaults.min, defaults.max);
    EXPECT_LE(defaults.max, hard.max);
    if (defaults.min < defaults.max) {
        EXPECT_FALSE(hive_type::is_within_hard_limits({defaults.max, defaults.min}));
    }

    hive_type hive;
    hive.insert_range(std::vector<int>{1, 2, 3, 4});
    std::unordered_map<int, const int*> addresses;
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        addresses.emplace(*it, std::addressof(*it));
    }
    const auto original_capacity = hive.capacity();
    hive.reserve(original_capacity); // no-op
    EXPECT_EQ(hive.capacity(), original_capacity);

    const auto requested = original_capacity + 64;
    auto stable_end = hive.end();
    hive.reserve(requested);
    EXPECT_GE(hive.capacity(), requested);
    EXPECT_EQ(stable_end, hive.end());
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        EXPECT_EQ(std::addressof(*it), addresses.at(*it));
    }

    const auto reserved_capacity = hive.capacity();
    hive.trim_capacity();
    EXPECT_LE(hive.capacity(), reserved_capacity);
    EXPECT_GE(hive.capacity(), hive.size());
    EXPECT_EQ(stable_end, hive.end());

    const auto before_trim_n = hive.capacity();
    hive.trim_capacity(before_trim_n + 100); // n >= capacity: no effect.
    EXPECT_EQ(hive.capacity(), before_trim_n);
    hive.trim_capacity(hive.size());
    EXPECT_GE(hive.capacity(), hive.size());
    EXPECT_LE(hive.capacity(), before_trim_n);
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        EXPECT_EQ(std::addressof(*it), addresses.at(*it));
    }

    const auto before_shrink = hive.capacity();
    const auto before_shrink_values = sorted(values(hive));
    hive.shrink_to_fit();
    EXPECT_GE(hive.capacity(), hive.size());
    EXPECT_LE(hive.capacity(), before_shrink);
    EXPECT_EQ(sorted(values(hive)), before_shrink_values);

    const std::hive_limits new_limits{defaults.min, defaults.min};
    hive.reshape(new_limits);
    EXPECT_EQ(hive.size(), 4u);
    EXPECT_EQ(hive.block_capacity_limits().min, new_limits.min);
    EXPECT_EQ(hive.block_capacity_limits().max, new_limits.max);
    EXPECT_EQ(sorted(values(hive)), before_shrink_values);

    if (hive.max_size() < std::numeric_limits<hive_type::size_type>::max()) {
        EXPECT_THROW(hive.reserve(hive.max_size() + 1), std::length_error);
    }
}

} // namespace

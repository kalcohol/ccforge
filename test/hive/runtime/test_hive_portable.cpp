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

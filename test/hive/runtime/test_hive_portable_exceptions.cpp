#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;

struct element_control {
    long long construction_budget = -1;
    std::size_t constructions = 0;
    std::size_t destructions = 0;
    std::size_t live = 0;
};

struct throwing_value {
    static inline element_control* control = nullptr;

    int value;

    static void before_construction() {
        if (control == nullptr) return;
        if (control->construction_budget == 0) throw std::runtime_error("injected element construction failure");
        if (control->construction_budget > 0) --control->construction_budget;
        ++control->constructions;
    }

    explicit throwing_value(int input) : value(input) {
        before_construction();
        if (control != nullptr) ++control->live;
    }
    throwing_value(const throwing_value& other) : value(other.value) {
        before_construction();
        if (control != nullptr) ++control->live;
    }
    throwing_value(throwing_value&& other) : value(other.value) {
        before_construction();
        if (control != nullptr) ++control->live;
    }
    throwing_value& operator=(const throwing_value&) = default;
    throwing_value& operator=(throwing_value&&) = default;
    ~throwing_value() {
        if (control != nullptr) {
            --control->live;
            ++control->destructions;
        }
    }
};

struct control_guard {
    element_control control;
    control_guard() { throwing_value::control = &control; }
    ~control_guard() { throwing_value::control = nullptr; }
};

template<class Allocator>
std::vector<int> values(const std::hive<throwing_value, Allocator>& hive) {
    std::vector<int> result;
    for (const auto& item : hive) result.push_back(item.value);
    return result;
}

template<class Allocator>
std::unordered_map<int, const throwing_value*> addresses(
    const std::hive<throwing_value, Allocator>& hive) {
    std::unordered_map<int, const throwing_value*> result;
    for (const auto& item : hive) result.emplace(item.value, std::addressof(item));
    return result;
}

template<class Allocator>
void expect_unchanged(const std::hive<throwing_value, Allocator>& hive,
                      const std::vector<int>& expected_values,
                      const std::unordered_map<int, const throwing_value*>& expected_addresses,
                      std::size_t expected_capacity) {
    EXPECT_EQ(values(hive), expected_values);
    EXPECT_EQ(hive.size(), expected_values.size());
    EXPECT_EQ(hive.capacity(), expected_capacity);
    ASSERT_EQ(hive.size(), expected_addresses.size());
    for (const auto& item : hive) {
        EXPECT_EQ(std::addressof(item), expected_addresses.at(item.value));
    }
}

TEST(HiveExceptions, ElementConstructionFailureOnEmptyHiveRollsBackCapacity) {
    using allocator_type = stateful_allocator<throwing_value>;
    control_guard guard;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<throwing_value, allocator_type> hive(allocator_type(state, 1));
        const auto initial_capacity = hive.capacity();
        guard.control.construction_budget = 0;
        EXPECT_THROW(hive.emplace(42), std::runtime_error);
        EXPECT_TRUE(hive.empty());
        EXPECT_EQ(hive.capacity(), initial_capacity);
        EXPECT_EQ(guard.control.live, 0u);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveExceptions, FailedEmplaceWithExistingHolesPreservesSizeCapacityOrderAndAddresses) {
    using allocator_type = stateful_allocator<throwing_value>;
    control_guard guard;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<throwing_value, allocator_type> hive(allocator_type(state, 1));
        for (int index = 0; index < 64; ++index) hive.emplace(index);
        for (auto it = hive.begin(); it != hive.end();) {
            if (it->value % 3 == 0) it = hive.erase(it);
            else ++it;
        }
        const auto old_values = values(hive);
        const auto old_addresses = addresses(hive);
        const auto old_capacity = hive.capacity();
        const auto old_size = hive.size();
        ASSERT_EQ(guard.control.live, old_size);

        guard.control.construction_budget = 0;
        EXPECT_THROW(hive.emplace(999), std::runtime_error);
        expect_unchanged(hive, old_values, old_addresses, old_capacity);
        EXPECT_EQ(guard.control.live, old_size);

        guard.control.construction_budget = -1;
        hive.emplace(777); // A failed free-slot attempt must not poison reuse.
        EXPECT_EQ(hive.size(), old_size + 1);
        EXPECT_GE(hive.capacity(), hive.size());
        for (const auto& [value, address] : old_addresses) {
            EXPECT_EQ(std::addressof(*hive.get_iterator(address)), address);
            EXPECT_EQ(hive.get_iterator(address)->value, value);
        }
        hive.clear();
        EXPECT_EQ(guard.control.live, 0u);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveExceptions, FullCapacityBlockAllocationFailureIsStrong) {
    using allocator_type = stateful_allocator<throwing_value>;
    control_guard guard;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<throwing_value, allocator_type> hive(allocator_type(state, 1));
        hive.emplace(0);
        int next = 1;
        while (hive.size() < hive.capacity()) hive.emplace(next++);
        ASSERT_EQ(hive.size(), hive.capacity());
        const auto old_values = values(hive);
        const auto old_addresses = addresses(hive);
        const auto old_capacity = hive.capacity();
        const auto old_constructions = guard.control.constructions;

        state->fail_after = 0;
        EXPECT_THROW(hive.emplace(1000), std::bad_alloc);
        expect_unchanged(hive, old_values, old_addresses, old_capacity);
        EXPECT_EQ(guard.control.constructions, old_constructions);

        state->fail_after = -1;
        guard.control.construction_budget = 0;
        EXPECT_THROW(hive.emplace(1001), std::runtime_error);
        expect_unchanged(hive, old_values, old_addresses, old_capacity);

        guard.control.construction_budget = -1;
        hive.emplace(1002);
        EXPECT_EQ(hive.size(), old_values.size() + 1);
        for (const auto& [value, address] : old_addresses) {
            EXPECT_EQ(hive.get_iterator(address)->value, value);
        }
    }
    EXPECT_EQ(guard.control.live, 0u);
    EXPECT_TRUE(state->clean());
}

TEST(HiveExceptions, ReservedBlockElementConstructionFailureIsStrong) {
    using allocator_type = stateful_allocator<throwing_value>;
    control_guard guard;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<throwing_value, allocator_type> hive(allocator_type(state, 1));
        hive.emplace(0);
        int next = 1;
        while (hive.size() < hive.capacity()) hive.emplace(next++);
        ASSERT_EQ(hive.size(), hive.capacity());
        const auto active_capacity = hive.capacity();
        const auto reserve_increment =
            3 * std::hive<throwing_value, allocator_type>::block_capacity_default_limits().min;
        hive.reserve(active_capacity + reserve_increment);
        ASSERT_GT(hive.capacity(), active_capacity);
        ASSERT_EQ(hive.size(), active_capacity); // Active blocks are full; spare blocks are reserved.

        const auto old_values = values(hive);
        const auto old_addresses = addresses(hive);
        const auto old_capacity = hive.capacity();
        const auto old_live = guard.control.live;
        guard.control.construction_budget = 0;
        EXPECT_THROW(hive.emplace(999), std::runtime_error);
        expect_unchanged(hive, old_values, old_addresses, old_capacity);
        EXPECT_EQ(guard.control.live, old_live);
        EXPECT_TRUE(state->valid_deallocations);

        guard.control.construction_budget = -1;
        hive.emplace(1000); // The same reserved-block path remains usable.
        EXPECT_EQ(hive.size(), old_values.size() + 1);
        EXPECT_EQ(guard.control.live, old_live + 1);
        for (const auto& [value, address] : old_addresses) {
            EXPECT_EQ(hive.get_iterator(address)->value, value);
        }
        hive.clear();
        EXPECT_EQ(guard.control.live, 0u);
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

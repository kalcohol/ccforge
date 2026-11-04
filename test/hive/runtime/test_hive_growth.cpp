#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"
#include "hive_test_fancy_allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;
using int_hive = std::hive<int, stateful_allocator<int>>;

struct bounded_state {
    std::shared_ptr<allocation_state> allocations = std::make_shared<allocation_state>();
    std::size_t element_max = 31;
    std::size_t rebound_max = 3;
    bool oversized_request = false;
};

// Rebinds deliberately have a smaller instance limit than allocator<int>.
template<class T>
struct bounded_allocator {
    using value_type = T;
    std::shared_ptr<bounded_state> state;

    bounded_allocator() : state(std::make_shared<bounded_state>()) {}
    explicit bounded_allocator(std::shared_ptr<bounded_state> input) : state(input) {}
    template<class U>
    bounded_allocator(const bounded_allocator<U>& other) noexcept : state(other.state) {}

    std::size_t max_size() const noexcept {
        const auto bound = std::is_same_v<T, int> ? state->element_max : state->rebound_max;
        return std::min(bound, stateful_allocator<T>(state->allocations, 7).max_size());
    }
    T* allocate(std::size_t count) {
        if (count > max_size()) {
            state->oversized_request = true;
            throw std::bad_array_new_length();
        }
        return stateful_allocator<T>(state->allocations, 7).allocate(count);
    }
    void deallocate(T* pointer, std::size_t count) noexcept {
        stateful_allocator<T>(state->allocations, 7).deallocate(pointer, count);
    }
    friend bool operator==(const bounded_allocator&, const bounded_allocator&) = default;
};

TEST(HiveGrowth, GeometricInsertionReducesDeterministicAllocationCount) {
    auto state = std::make_shared<allocation_state>();
    {
        int_hive hive(std::hive_limits{4, 64}, stateful_allocator<int>(state, 1));
        std::vector<std::size_t> increments;
        for (int value = 0; value != 1024; ++value) {
            const auto before = hive.capacity();
            hive.emplace(value);
            if (hive.capacity() != before) increments.push_back(hive.capacity() - before);
        }
        ASSERT_EQ(increments.size(), 20u);
        EXPECT_EQ((std::vector<std::size_t>(increments.begin(), increments.begin() + 6)),
                  (std::vector<std::size_t>{4, 4, 8, 16, 32, 64}));
        for (const auto increment : increments) {
            EXPECT_GE(increment, 4u);
            EXPECT_LE(increment, 64u);
        }
        EXPECT_EQ(hive.capacity(), 1024u);
        // Scheme S: one control plus header, payload and skipfield per block.
        // A minimum-only allocation policy would require 769 allocations.
        EXPECT_EQ(state->allocation_attempts, 61u);
        int sum = 0;
        for (const int value : hive) sum += value;
        EXPECT_EQ(sum, 1023 * 1024 / 2);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGrowth, InsertionClampsToFinalAggregateHeadroom) {
    auto state = std::make_shared<allocation_state>();
    state->artificial_int_max_size = 11;
    {
        int_hive hive(std::hive_limits{1, 3}, stateful_allocator<int>(state, 1));
        std::vector<std::size_t> increments;
        for (int value = 0; value != 11; ++value) {
            const auto before = hive.capacity();
            hive.emplace(value);
            if (hive.capacity() != before) increments.push_back(hive.capacity() - before);
        }
        EXPECT_EQ(increments, (std::vector<std::size_t>{1, 1, 2, 3, 3, 1}));
        EXPECT_EQ(hive.capacity(), 11u);
        const auto attempts = state->allocation_attempts;
        EXPECT_THROW(hive.emplace(11), std::length_error);
        EXPECT_THROW(hive.reserve(12), std::length_error);
        EXPECT_EQ(state->allocation_attempts, attempts);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGrowth, ReservePacksLegalMinimumTailsWithinHeadroom) {
    auto state = std::make_shared<allocation_state>();
    state->artificial_int_max_size = 16;
    {
        int_hive hive(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
        const auto end = hive.end();
        ASSERT_NO_THROW(hive.reserve(11));
        EXPECT_EQ(hive.capacity(), 16u); // Two legal blocks must be 8 + 8.
        EXPECT_EQ(state->allocation_attempts, 7u);
        EXPECT_EQ(hive.end(), end);
        const auto attempts = state->allocation_attempts;
        for (int value = 0; value != 16; ++value) hive.emplace(value);
        EXPECT_EQ(state->allocation_attempts, attempts);
        EXPECT_THROW(hive.emplace(16), std::length_error);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGrowth, ExistingCapacityCannotUseAnUndersizedTail) {
    auto state = std::make_shared<allocation_state>();
    state->artificial_int_max_size = 15;
    {
        int_hive hive(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
        hive.emplace(0);
        ASSERT_EQ(hive.capacity(), 8u);
        const auto end = hive.end();
        const auto attempts = state->allocation_attempts;
        EXPECT_THROW(hive.reserve(9), std::length_error);
        EXPECT_EQ(hive.capacity(), 8u);
        EXPECT_EQ(hive.end(), end);
        for (int value = 1; value != 8; ++value) hive.emplace(value);
        const auto full_end = hive.end();
        EXPECT_THROW(hive.emplace(8), std::length_error);
        EXPECT_EQ(hive.end(), full_end);
        EXPECT_EQ(state->allocation_attempts, attempts);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGrowth, ReserveMatchesSmallExhaustiveLegalPackingOracle) {
    for (std::size_t minimum = 1; minimum <= 4; ++minimum) {
        for (std::size_t maximum = minimum; maximum <= minimum + 3; ++maximum) {
            for (std::size_t total = 1; total <= 18; ++total) {
                for (std::size_t requested = 1; requested <= total; ++requested) {
                    SCOPED_TRACE(::testing::Message() << minimum << ',' << maximum
                                 << ',' << total << ',' << requested);
                    // Enumerate representable totals independently of the planner.
                    std::size_t expected = 0;
                    for (std::size_t candidate = requested; candidate <= total && expected == 0;
                         ++candidate) {
                        for (std::size_t blocks = 1; blocks * minimum <= candidate; ++blocks) {
                            if (candidate <= blocks * maximum) {
                                expected = candidate;
                                break;
                            }
                        }
                    }
                    auto state = std::make_shared<allocation_state>();
                    state->artificial_int_max_size = total;
                    {
                        int_hive hive(std::hive_limits{minimum, maximum},
                                      stateful_allocator<int>(state, 1));
                        if (expected == 0) {
                            EXPECT_THROW(hive.reserve(requested), std::length_error);
                            EXPECT_EQ(state->allocation_attempts, 0u);
                        } else {
                            ASSERT_NO_THROW(hive.reserve(requested));
                            EXPECT_EQ(hive.capacity(), expected);
                            const auto attempts = state->allocation_attempts;
                            for (std::size_t value = 0; value != expected; ++value) {
                                hive.emplace(static_cast<int>(value));
                            }
                            EXPECT_EQ(state->allocation_attempts, attempts);
                        }
                    }
                    EXPECT_TRUE(state->clean());
                }
            }
        }
    }
}

TEST(HiveGrowth, ActualReboundAllocatorBoundAppliesToInsertionReserveAndShrink) {
    using hive_type = std::hive<int, bounded_allocator<int>>;
    auto state = std::make_shared<bounded_state>();
    {
        hive_type hive(std::hive_limits{2, 10}, bounded_allocator<int>(state));
        EXPECT_EQ(hive.max_size(), 31u);
        for (int value = 0; value != 11; ++value) hive.emplace(value);
        EXPECT_EQ(hive.capacity(), 13u); // 2 + 2 + 3 + 3 + 3.
        hive_type planned(std::hive_limits{2, 10}, bounded_allocator<int>(state));
        ASSERT_NO_THROW(planned.reserve(11));
        EXPECT_EQ(planned.capacity(), 11u); // 3 + 3 + 3 + 2.
        hive.shrink_to_fit();
        EXPECT_EQ(hive.capacity(), planned.capacity());
        EXPECT_EQ(hive.size(), 11u);
        EXPECT_FALSE(state->oversized_request);
    }
    EXPECT_TRUE(state->allocations->clean());

    state->rebound_max = 1;
    const auto attempts = state->allocations->allocation_attempts;
    {
        hive_type hive(std::hive_limits{2, 10}, bounded_allocator<int>(state));
        EXPECT_THROW(hive.emplace(0), std::length_error);
        EXPECT_THROW(hive.reserve(1), std::length_error);
        EXPECT_EQ(state->allocations->allocation_attempts, attempts);
        EXPECT_FALSE(state->oversized_request);
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveGrowth, ShrinkUsesReservePlanAndNeverIncreasesCapacity) {
    for (int count = 1; count <= 24; ++count) {
        SCOPED_TRACE(count);
        auto state = std::make_shared<allocation_state>();
        state->artificial_int_max_size = 24;
        {
            int_hive hive(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
            int_hive planned(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
            hive.reserve(24);
            for (int value = 0; value != count; ++value) hive.emplace(value);
            planned.reserve(count);
            const auto before = hive.capacity();
            hive.shrink_to_fit();
            EXPECT_LE(hive.capacity(), before);
            EXPECT_EQ(hive.capacity(), planned.capacity());
            EXPECT_EQ(hive.size(), static_cast<std::size_t>(count));
            auto actual = std::vector<int>(hive.begin(), hive.end());
            ASSERT_EQ(actual.size(), static_cast<std::size_t>(count));
            std::sort(actual.begin(), actual.end());
            for (int value = 0; value != count; ++value) EXPECT_EQ(actual[value], value);
            const auto attempts = state->allocation_attempts;
            hive.shrink_to_fit();
            EXPECT_EQ(state->allocation_attempts, attempts);
        }
        EXPECT_TRUE(state->clean());
    }
}

TEST(HiveGrowth, ReserveAllocationFailuresPreserveElementsEndAndLegalPublishedBlocks) {
    for (long long failure = 0; failure != 6; ++failure) {
        SCOPED_TRACE(failure);
        auto state = std::make_shared<allocation_state>();
        state->artificial_int_max_size = 24;
        {
            int_hive hive(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
            auto first = hive.emplace(42);
            const int* address = std::addressof(*first);
            const auto end = hive.end();
            state->fail_after = failure;
            EXPECT_THROW(hive.reserve(24), std::bad_alloc);
            EXPECT_EQ(hive.size(), 1u);
            EXPECT_EQ(hive.begin(), first);
            EXPECT_EQ(std::addressof(*first), address);
            EXPECT_EQ(*first, 42);
            EXPECT_EQ(hive.end(), end);
            EXPECT_EQ(hive.capacity(), failure < 3 ? 8u : 16u);
            state->fail_after = -1;
            ASSERT_NO_THROW(hive.reserve(24));
            EXPECT_EQ(hive.capacity(), 24u);
            EXPECT_EQ(hive.end(), end);
        }
        EXPECT_TRUE(state->clean());
    }
}

TEST(HiveGrowth, ShrinkAllocationFailuresOccurBeforeValuesOrIteratorsChange) {
    for (long long failure = 0; failure != 7; ++failure) {
        SCOPED_TRACE(failure);
        auto state = std::make_shared<allocation_state>();
        {
            int_hive hive(std::hive_limits{8, 10}, stateful_allocator<int>(state, 1));
            for (int value = 0; value != 24; ++value) hive.emplace(value);
            for (auto it = hive.begin(); it != hive.end();) {
                if (*it < 5 || (8 <= *it && *it < 13) || *it == 16) ++it;
                else it = hive.erase(it);
            }
            ASSERT_EQ(hive.size(), 11u);
            ASSERT_EQ(hive.capacity(), 26u); // Each of the three blocks is active.
            const auto values = std::vector<int>(hive.begin(), hive.end());
            std::vector<const int*> addresses;
            for (const int& value : hive) addresses.push_back(std::addressof(value));
            const auto end = hive.end();
            const auto allocations = state->allocations.size();
            state->fail_after = failure;
            EXPECT_THROW(hive.shrink_to_fit(), std::bad_alloc);
            EXPECT_EQ(hive.capacity(), 26u);
            EXPECT_EQ(hive.end(), end);
            EXPECT_EQ((std::vector<int>(hive.begin(), hive.end())), values);
            std::size_t index = 0;
            for (const int& value : hive) EXPECT_EQ(std::addressof(value), addresses[index++]);
            EXPECT_EQ(state->allocations.size(), allocations);
            state->fail_after = -1;
            hive.shrink_to_fit();
            EXPECT_EQ(hive.capacity(), 16u);
        }
        EXPECT_TRUE(state->clean());
    }
}

struct growth_value {
    static inline bool fail = false;
    int value;
    explicit growth_value(int input) : value(input) {
        if (fail) throw std::runtime_error("injected growth construction failure");
    }
};

TEST(HiveGrowth, NewGeometricBlockIsNotPublishedBeforeElementConstruction) {
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<growth_value, stateful_allocator<growth_value>> hive(
            std::hive_limits{2, 16}, stateful_allocator<growth_value>(state, 1));
        for (int value = 0; value != 8; ++value) hive.emplace(value);
        ASSERT_EQ(hive.capacity(), 8u);
        const auto first = hive.begin();
        const auto address = std::addressof(*first);
        const auto end = hive.end();
        const auto allocations = state->allocations.size();
        for (long long failure = 0; failure != 3; ++failure) {
            state->fail_after = failure;
            EXPECT_THROW(hive.emplace(8), std::bad_alloc);
            EXPECT_EQ(hive.capacity(), 8u);
            EXPECT_EQ(hive.size(), 8u);
            EXPECT_EQ(hive.end(), end);
            EXPECT_EQ(state->allocations.size(), allocations);
        }
        state->fail_after = -1;
        growth_value::fail = true;
        EXPECT_THROW(hive.emplace(8), std::runtime_error);
        growth_value::fail = false;
        EXPECT_EQ(hive.capacity(), 8u);
        EXPECT_EQ(hive.size(), 8u);
        EXPECT_EQ(hive.end(), end);
        EXPECT_EQ(state->allocations.size(), allocations);
        hive.emplace(8);
        EXPECT_EQ(hive.capacity(), 16u);
        EXPECT_EQ(std::addressof(*first), address);
        EXPECT_EQ(first->value, 0);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGrowth, FancyAllocatorHandlesSurviveGrowthReserveAndShrink) {
    using allocator_type = forge_hive_fancy_allocator_test::fancy_allocator<int>;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<int, allocator_type> hive(std::hive_limits{2, 16}, allocator_type(state, 7));
        auto first = hive.emplace(0);
        const auto address = std::addressof(*first);
        for (int value = 1; value != 65; ++value) hive.emplace(value);
        const auto end = hive.end();
        hive.reserve(100);
        EXPECT_EQ(hive.end(), end);
        EXPECT_EQ(std::addressof(*first), address);
        EXPECT_EQ(*first, 0);
        for (auto it = hive.begin(); it != hive.end();) {
            if (*it % 2 != 0) it = hive.erase(it);
            else ++it;
        }
        const auto before = hive.capacity();
        hive.shrink_to_fit();
        EXPECT_LE(hive.capacity(), before);
        EXPECT_EQ(hive.size(), 33u);
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

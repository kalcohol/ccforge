#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"
#include "hive_test_fancy_allocator.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;
using forge_hive_fancy_allocator_test::fancy_allocator;

constexpr std::size_t compact_block_max = std::numeric_limits<std::uint16_t>::max();
constexpr std::size_t reuse_rounds = 65552;
constexpr bool conventional_64bit_slot_abi =
    sizeof(void*) == 8 && sizeof(std::size_t) == 8 && alignof(std::size_t) == 8 &&
    sizeof(std::uint16_t) == 2 && alignof(std::uint16_t) == 2 &&
    sizeof(bool) == 1 && alignof(bool) == 1;

struct allocation_sample {
    std::size_t count;
    std::size_t object_size;
    std::size_t alignment;
};

struct layout_probe_state {
    std::shared_ptr<allocation_state> allocations = std::make_shared<allocation_state>();
    std::vector<allocation_sample> samples;
};

// Observe rebound allocation requests without naming or accessing private hive types.
template<class T>
class layout_probe_allocator {
    template<class>
    friend class layout_probe_allocator;

public:
    using value_type = T;
    using is_always_equal = std::false_type;

    template<class U>
    struct rebind { using other = layout_probe_allocator<U>; };

    layout_probe_allocator() : state_(std::make_shared<layout_probe_state>()) {}
    explicit layout_probe_allocator(std::shared_ptr<layout_probe_state> state) noexcept
        : state_(std::move(state)) {}

    template<class U>
    layout_probe_allocator(const layout_probe_allocator<U>& other) noexcept
        : state_(other.state_) {}

    T* allocate(std::size_t count) {
        stateful_allocator<T> underlying(state_->allocations, 1);
        T* const result = underlying.allocate(count);
        try {
            state_->samples.push_back({count, sizeof(T), alignof(T)});
        } catch (...) {
            underlying.deallocate(result, count);
            throw;
        }
        return result;
    }

    void deallocate(T* pointer, std::size_t count) noexcept {
        stateful_allocator<T>(state_->allocations, 1).deallocate(pointer, count);
    }

    std::size_t max_size() const noexcept {
        return stateful_allocator<T>(state_->allocations, 1).max_size();
    }

    bool operator==(const layout_probe_allocator& other) const noexcept {
        return state_ == other.state_;
    }

private:
    std::shared_ptr<layout_probe_state> state_;
};

template<class T>
void measure_layout(const std::string& name, std::size_t slot_budget = 0) {
    using allocator_type = layout_probe_allocator<T>;
    constexpr std::size_t block_capacity = 64;
    auto state = std::make_shared<layout_probe_state>();
    {
        std::hive<T, allocator_type> hive(
            std::hive_limits{block_capacity, block_capacity}, allocator_type(state));
        hive.reserve(block_capacity);
        ASSERT_EQ(hive.capacity(), block_capacity);

        const allocation_sample* slots = nullptr;
        std::size_t allocated_bytes = 0;
        for (const auto& sample : state->samples) {
            allocated_bytes += sample.count * sample.object_size;
            if (sample.count == block_capacity) {
                ASSERT_EQ(slots, nullptr);
                slots = std::addressof(sample);
            }
        }
        ASSERT_NE(slots, nullptr);
        EXPECT_GE(slots->object_size, sizeof(T));
        EXPECT_GE(slots->alignment, alignof(T));
        EXPECT_EQ(slots->object_size % slots->alignment, 0u);
        if (slot_budget != 0) EXPECT_LE(slots->object_size, slot_budget);

        testing::Test::RecordProperty(name + "_slot_bytes", std::to_string(slots->object_size));
        testing::Test::RecordProperty(name + "_slot_alignment", std::to_string(slots->alignment));
        testing::Test::RecordProperty(name + "_allocation_count", std::to_string(state->samples.size()));
        testing::Test::RecordProperty(name + "_allocated_bytes", std::to_string(allocated_bytes));
        testing::Test::RecordProperty(name + "_header_bytes",
            std::to_string(allocated_bytes - block_capacity * slots->object_size));
        hive.emplace();
        EXPECT_EQ(hive.get_iterator(std::addressof(*hive.begin())), hive.begin());
    }
    EXPECT_TRUE(state->allocations->clean());
}

struct large_value {
    std::size_t words[4]{};
};

struct alignas(16) aligned_16_value {
    int value = 0;
};

struct alignas(64) aligned_64_value {
    int value = 0;
};

TEST(HiveLayout, OrdinaryFourByteSlotFitsTwentyFourBytes) {
    if (!conventional_64bit_slot_abi || sizeof(int) != 4 || alignof(int) > 4) {
        GTEST_SKIP() << "The 24-byte budget targets ordinary 64-bit ABIs";
    }
    measure_layout<int>("int", 24);
}

TEST(HiveLayout, RecordDoubleLargeAndOveralignedLayoutsSeparately) {
    measure_layout<double>("double",
        conventional_64bit_slot_abi && sizeof(double) == 8 && alignof(double) <= 8 ? 24 : 0);
    measure_layout<large_value>("large", conventional_64bit_slot_abi ? 48 : 0);
    measure_layout<aligned_16_value>("aligned16",
        conventional_64bit_slot_abi && sizeof(aligned_16_value) == 16 ? 32 : 0);
    measure_layout<aligned_64_value>("aligned64",
        conventional_64bit_slot_abi && sizeof(aligned_64_value) == 64 ? 128 : 0);
}

TEST(HiveLayout, CompactHardLimitRejectsTheSentinelAsAnIndexNotAsACount) {
    using hive_type = std::hive<int>;
    if (sizeof(std::size_t) < 8) GTEST_SKIP() << "Conventional 64-bit hard-limit control";
    EXPECT_EQ(hive_type::block_capacity_hard_limits().max, compact_block_max);
    EXPECT_TRUE(hive_type::is_within_hard_limits({compact_block_max, compact_block_max}));
    EXPECT_FALSE(hive_type::is_within_hard_limits({1, compact_block_max + 1}));
    EXPECT_THROW((hive_type(std::hive_limits{1, compact_block_max + 1})), std::length_error);
}

TEST(HiveLayout, TotalPopulationAndReserveExceedOneCompactBlock) {
    using allocator_type = stateful_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    if (hive_type::block_capacity_hard_limits().max < compact_block_max) {
        GTEST_SKIP() << "The allocator or ABI cannot represent the compact boundary";
    }
    auto state = std::make_shared<allocation_state>();
    {
        hive_type hive(std::hive_limits{compact_block_max, compact_block_max},
                       allocator_type(state, 1));
        ASSERT_GT(hive.max_size(), compact_block_max);
        hive.reserve(compact_block_max + 1);
        ASSERT_EQ(hive.capacity(), compact_block_max * 2);
        const auto allocation_attempts = state->allocation_attempts;
        for (std::size_t index = 0; index < compact_block_max; ++index) {
            hive.emplace(static_cast<int>(index));
        }
        auto boundary = std::prev(hive.end());
        const int* const boundary_address = std::addressof(*boundary);
        auto next_block = hive.emplace(static_cast<int>(compact_block_max));
        EXPECT_EQ(hive.size(), compact_block_max + 1);
        EXPECT_EQ(state->allocation_attempts, allocation_attempts);
        EXPECT_EQ(std::next(boundary), next_block);
        EXPECT_EQ(std::prev(next_block), boundary);
        EXPECT_LT(boundary, next_block);
        EXPECT_EQ(hive.get_iterator(boundary_address), boundary);
        auto successor = hive.erase(boundary);
        EXPECT_EQ(successor, next_block);
        EXPECT_EQ(std::distance(hive.begin(), hive.end()),
                  static_cast<hive_type::difference_type>(hive.size()));
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveLayout, LastPhysicalSlotAndFreeListSentinelRoundTrip) {
    using allocator_type = stateful_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    for (const std::size_t capacity : {std::size_t{1}, std::size_t{2},
                                       compact_block_max - 1, compact_block_max}) {
        SCOPED_TRACE(capacity);
        if (hive_type::block_capacity_hard_limits().max < capacity) continue;
        auto state = std::make_shared<allocation_state>();
        {
            hive_type hive(std::hive_limits{capacity, capacity}, allocator_type(state, 1));
            hive.reserve(capacity);
            for (std::size_t index = 0; index < capacity; ++index) {
                hive.emplace(static_cast<int>(index));
            }
            ASSERT_EQ(hive.capacity(), capacity);
            auto first = hive.begin();
            auto last = std::prev(hive.end());
            const int* const last_address = std::addressof(*last);
            for (int round = 0; round < 8; ++round) {
                auto predecessor = capacity > 1 ? std::prev(last) : last;
                auto successor = hive.erase(last);
                ASSERT_EQ(successor, hive.end());
                last = hive.emplace(-round - 1);
                EXPECT_EQ(std::addressof(*last), last_address);
                EXPECT_EQ(hive.get_iterator(last_address), last);
                EXPECT_EQ(std::next(last), hive.end());
                if (capacity > 1) {
                    EXPECT_EQ(*first, 0);
                    EXPECT_LT(first, last);
                    EXPECT_EQ(std::next(predecessor), last);
                    EXPECT_EQ(std::prev(last), predecessor);
                } else {
                    first = last;
                    EXPECT_EQ(hive.begin(), last);
                }
            }
            const auto allocation_attempts = state->allocation_attempts;
            auto next_block = hive.emplace(999);
            EXPECT_EQ(hive.size(), capacity + 1);
            EXPECT_EQ(hive.capacity(), capacity * 2);
            EXPECT_GT(state->allocation_attempts, allocation_attempts);
            EXPECT_LT(last, next_block);
            EXPECT_EQ(std::next(last), next_block);
            EXPECT_EQ(std::prev(next_block), last);
            EXPECT_EQ(std::addressof(*last), last_address);
        }
        EXPECT_TRUE(state->clean());
    }
}

TEST(HiveLayout, HeadMiddleAndTailHolesPreserveRetainedTraversalAndOrdering) {
    using allocator_type = stateful_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    auto state = std::make_shared<allocation_state>();
    {
        hive_type hive(std::hive_limits{8, 8}, allocator_type(state, 1));
        std::vector<hive_type::iterator> initial;
        std::vector<const int*> addresses;
        for (int value = 0; value < 8; ++value) {
            initial.push_back(hive.emplace(value));
            addresses.push_back(std::addressof(*initial.back()));
        }
        auto after_head = hive.erase(initial[0]);
        EXPECT_EQ(after_head, initial[1]);
        auto after_middle = hive.erase(initial[4]);
        EXPECT_EQ(after_middle, initial[5]);
        auto after_tail = hive.erase(initial[7]);
        EXPECT_EQ(after_tail, hive.end());

        std::vector<hive_type::iterator> expected{
            initial[1], initial[2], initial[3], initial[5], initial[6]};
        for (int value = 100; value < 103; ++value) expected.push_back(hive.emplace(value));
        ASSERT_EQ(hive.capacity(), 8u);
        auto current = hive.begin();
        for (auto retained : expected) {
            ASSERT_NE(current, hive.end());
            EXPECT_EQ(current, retained);
            EXPECT_EQ(hive.get_iterator(std::addressof(*retained)), retained);
            ++current;
        }
        EXPECT_EQ(current, hive.end());
        auto reverse = hive.rbegin();
        for (auto expected_reverse = expected.rbegin(); expected_reverse != expected.rend();
             ++expected_reverse, ++reverse) {
            ASSERT_NE(reverse, hive.rend());
            EXPECT_EQ(std::addressof(*reverse), std::addressof(**expected_reverse));
        }
        EXPECT_EQ(reverse, hive.rend());
        for (const std::size_t index : {1u, 2u, 3u, 5u, 6u}) {
            EXPECT_EQ(std::addressof(*initial[index]), addresses[index]);
            EXPECT_EQ(*initial[index], static_cast<int>(index));
        }
        for (std::size_t left = 0; left < expected.size(); ++left) {
            const hive_type::const_iterator const_left = expected[left];
            for (std::size_t right = 0; right < expected.size(); ++right) {
                const auto order = left < right ? std::strong_ordering::less
                    : left > right ? std::strong_ordering::greater
                                   : std::strong_ordering::equivalent;
                EXPECT_EQ(expected[left] <=> expected[right], order);
                EXPECT_EQ(const_left <=> expected[right], order);
                EXPECT_EQ(const_left == expected[right], left == right);
            }
        }
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveLayout, ReusedSlotOrdersRemainWideWithPermanentSurvivors) {
    std::hive<int> hive(std::hive_limits{4, 4});
    const auto first = hive.emplace(0);
    const auto second = hive.emplace(1);
    const auto third = hive.emplace(2);
    auto rolling = hive.emplace(3);
    const int* const address = std::addressof(*rolling);
    for (std::size_t round = 0; round < reuse_rounds; ++round) {
        auto successor = hive.erase(rolling);
        ASSERT_EQ(successor, hive.end());
        rolling = hive.emplace(3);
        ASSERT_EQ(std::addressof(*rolling), address);
        ASSERT_LT(third, rolling);
        ASSERT_EQ(std::next(third), rolling);
    }
    EXPECT_LT(first, second);
    EXPECT_LT(second, third);
    EXPECT_EQ(hive.get_iterator(address), rolling);
    EXPECT_EQ(hive.capacity(), 4u);
}

TEST(HiveLayout, ReactivatedBlockOrdersRemainWideWithAPermanentBlock) {
    std::hive<int> hive(std::hive_limits{1, 1});
    const auto retained = hive.emplace(0);
    auto rolling = hive.emplace(1);
    const int* const retained_address = std::addressof(*retained);
    for (std::size_t round = 0; round < reuse_rounds; ++round) {
        auto successor = hive.erase(rolling);
        ASSERT_EQ(successor, hive.end());
        rolling = hive.emplace(1);
        ASSERT_LT(retained, rolling);
        ASSERT_EQ(std::next(retained), rolling);
        ASSERT_EQ(std::prev(rolling), retained);
    }
    EXPECT_EQ(std::addressof(*retained), retained_address);
    EXPECT_EQ(*retained, 0);
    EXPECT_EQ(hive.capacity(), 2u);
}

TEST(HiveLayout, FancyPointerLookupReuseAndSpliceRetainOrder) {
    using allocator_type = fancy_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    using const_pointer = hive_type::const_pointer;
    auto state = std::make_shared<allocation_state>();
    {
        hive_type destination(std::hive_limits{4, 4}, allocator_type(state, 7));
        std::vector<hive_type::iterator> initial;
        for (int value = 0; value < 4; ++value) initial.push_back(destination.emplace(value));
        auto retained = initial[2];
        const_pointer retained_pointer = std::pointer_traits<const_pointer>::pointer_to(
            std::as_const(*retained));
        const int* const retained_address = std::to_address(retained_pointer);
        const int* const hole_address = std::addressof(*initial[1]);
        auto successor = destination.erase(initial[1]);
        EXPECT_EQ(successor, retained);
        auto reused = destination.emplace(99);
        EXPECT_EQ(std::addressof(*reused), hole_address);
        EXPECT_LT(retained, reused);
        EXPECT_EQ(destination.get_iterator(retained_pointer), retained);

        hive_type source(std::hive_limits{4, 4}, allocator_type(state, 7));
        auto migrated = source.emplace(10);
        for (int value = 11; value < 14; ++value) source.emplace(value);
        const_pointer migrated_pointer = std::pointer_traits<const_pointer>::pointer_to(
            std::as_const(*migrated));
        destination.splice(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_iterator(migrated_pointer), migrated);
        EXPECT_EQ(std::as_const(destination).get_iterator(retained_pointer),
                  hive_type::const_iterator(retained));
        EXPECT_EQ(std::addressof(*retained), retained_address);
        EXPECT_LT(reused, migrated);
        EXPECT_EQ(std::next(reused), migrated);
        EXPECT_EQ(std::prev(migrated), reused);
    }
    EXPECT_TRUE(state->clean());
}

} // namespace

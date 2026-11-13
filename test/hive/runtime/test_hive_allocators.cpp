#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <memory_resource>
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


struct trivial_allocator_constructor {
    trivial_allocator_constructor() = default;
};

struct constexpr_allocator_constructor {
    constexpr constexpr_allocator_constructor() noexcept {}
};

struct runtime_allocator_constructor {
    inline static int calls = 0;
    runtime_allocator_constructor() { ++calls; }
};

struct nonliteral_allocator_constructor {
    inline static int destructions = 0;
    constexpr nonliteral_allocator_constructor() noexcept = default;
    ~nonliteral_allocator_constructor() noexcept { ++destructions; }
};

template<class T, class Constructor, std::size_t Bound = 4>
struct fixed_limit_allocator : Constructor {
    using value_type = T;
    using is_always_equal = std::true_type;
    template<class U>
    struct rebind { using other = fixed_limit_allocator<U, Constructor, Bound>; };

    fixed_limit_allocator() = default;
    template<class U>
    constexpr fixed_limit_allocator(
        const fixed_limit_allocator<U, Constructor, Bound>&) noexcept {}

    constexpr std::size_t max_size() const noexcept {
        return std::min(Bound, (std::numeric_limits<std::size_t>::max)() / sizeof(T));
    }

    T* allocate(std::size_t n) {
        if (n > max_size()) throw std::bad_array_new_length();
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* pointer, std::size_t n) noexcept {
        std::allocator<T>{}.deallocate(pointer, n);
    }

    template<class U>
    constexpr bool operator==(
        const fixed_limit_allocator<U, Constructor, Bound>&) const noexcept {
        return true;
    }
};

template<class Constructor>
void check_fixed_allocator_limits() {
    using allocator_type = fixed_limit_allocator<int, Constructor>;
    using hive_type = std::hive<int, allocator_type>;
    static_assert(std::is_empty_v<allocator_type>);
    static_assert(std::is_nothrow_default_constructible_v<allocator_type>);
    static_assert(std::is_trivially_default_constructible_v<allocator_type> ==
                  std::is_same_v<Constructor, trivial_allocator_constructor>);
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    static_assert(hard.max == 4);
    static_assert(defaults.min <= 4 && defaults.max <= 4);
    static_assert(hive_type::is_within_hard_limits(defaults));
    static_assert(noexcept(hive_type::block_capacity_hard_limits()));
    static_assert(noexcept(hive_type::block_capacity_default_limits()));

    // An independent explicit-limit control shows the allocator is usable.
    hive_type explicit_limits(std::hive_limits{1, 1}, allocator_type{});
    ASSERT_NO_THROW(explicit_limits.emplace(17));
    ASSERT_EQ(explicit_limits.size(), 1u);
    EXPECT_EQ(*explicit_limits.begin(), 17);

    hive_type ordinary(allocator_type{});
    EXPECT_EQ(ordinary.block_capacity_limits().min, defaults.min);
    EXPECT_EQ(ordinary.block_capacity_limits().max, defaults.max);
    EXPECT_EQ(ordinary.max_size(), 4u);
    ASSERT_NO_THROW(ordinary.reserve(4));
    for (int value = 0; value != 4; ++value) {
        ASSERT_NO_THROW(ordinary.emplace(value));
    }
    EXPECT_EQ(values(ordinary), (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(ordinary.capacity(), 4u);
    EXPECT_THROW(ordinary.emplace(4), std::length_error);
    EXPECT_EQ(ordinary.size(), 4u);
    EXPECT_EQ(ordinary.capacity(), 4u);
}

TEST(HiveAllocator, TrivialFixedBoundControlsDefaultGrowth) {
    check_fixed_allocator_limits<trivial_allocator_constructor>();
}

TEST(HiveAllocator, NontrivialConstexprFixedBoundControlsDefaultGrowth) {
    check_fixed_allocator_limits<constexpr_allocator_constructor>();
}

template<class Constructor>
void check_runtime_only_static_limits() {
    using allocator_type = fixed_limit_allocator<int, Constructor, 64>;
    using hive_type = std::hive<int, allocator_type>;
    static_assert(std::is_empty_v<allocator_type>);
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    constexpr auto ordinary_hard = std::hive<int>::block_capacity_hard_limits();
    constexpr auto ordinary_defaults = std::hive<int>::block_capacity_default_limits();
    static_assert(hard.max == ordinary_hard.max);
    static_assert(defaults.min == ordinary_defaults.min);
    static_assert(defaults.max == ordinary_defaults.max);
    static_assert(noexcept(hive_type::block_capacity_hard_limits()));
    static_assert(noexcept(hive_type::block_capacity_default_limits()));
    EXPECT_EQ(hive_type::block_capacity_hard_limits().max, hard.max);
    EXPECT_EQ(hive_type::block_capacity_default_limits().min, defaults.min);
}

TEST(HiveAllocator, StaticLimitsDoNotInvokeRuntimeDefaultConstructor) {
    runtime_allocator_constructor::calls = 0;
    check_runtime_only_static_limits<runtime_allocator_constructor>();
    EXPECT_EQ(runtime_allocator_constructor::calls, 0);

    using allocator_type = fixed_limit_allocator<int, runtime_allocator_constructor, 64>;
    std::hive<int, allocator_type> hive(allocator_type{});
    ASSERT_NO_THROW(hive.emplace(23));
    EXPECT_EQ(*hive.begin(), 23);
}

TEST(HiveAllocator, StaticLimitsAcceptEmptyNonliteralAllocator) {
    nonliteral_allocator_constructor::destructions = 0;
    check_runtime_only_static_limits<nonliteral_allocator_constructor>();
    EXPECT_EQ(nonliteral_allocator_constructor::destructions, 0);

    using allocator_type = fixed_limit_allocator<int, nonliteral_allocator_constructor, 64>;
    std::hive<int, allocator_type> hive(allocator_type{});
    ASSERT_NO_THROW(hive.emplace(23));
    EXPECT_EQ(*hive.begin(), 23);
}

template<class T>
struct no_default_allocator : stateful_allocator<T> {
    using base = stateful_allocator<T>;
    using base::base;
    template<class U>
    struct rebind { using other = no_default_allocator<U>; };

    no_default_allocator() = delete;
    template<class U>
    no_default_allocator(const no_default_allocator<U>& other) noexcept
        : base(static_cast<const stateful_allocator<U>&>(other)) {}

    no_default_allocator select_on_container_copy_construction() const noexcept {
        return *this;
    }
};

TEST(HiveAllocator, NonDefaultAllocatorRetainsDefaultGrowth) {
    using allocator_type = no_default_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    static_assert(!std::is_default_constructible_v<allocator_type>);
    static_assert(std::is_same_v<
        decltype(std::declval<const allocator_type&>().select_on_container_copy_construction()),
        allocator_type>);
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    static_assert(hard.max == std::hive<int>::block_capacity_hard_limits().max);
    static_assert(defaults.min == std::hive<int>::block_capacity_default_limits().min);
    static_assert(noexcept(hive_type::block_capacity_hard_limits()));

    auto state = std::make_shared<allocation_state>();
    {
        hive_type hive{allocator_type(state, 19)};
        ASSERT_NO_THROW(hive.emplace(31));
        EXPECT_EQ(*hive.begin(), 31);
        EXPECT_EQ(hive.get_allocator().id(), 19);
        EXPECT_EQ(hive.block_capacity_limits().min, defaults.min);
        EXPECT_EQ(hive.capacity(), defaults.min);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, StatefulLimitsStayPerInstanceAndKeepBlockRounding) {
    using allocator_type = stateful_allocator<int>;
    using hive_type = std::hive<int, allocator_type>;
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    static_assert(defaults.min == std::hive<int>::block_capacity_default_limits().min);
    static_assert(defaults.min > 1);
    auto small = std::make_shared<allocation_state>();
    auto ordinary = std::make_shared<allocation_state>();
    small->artificial_int_max_size = 4;
    ordinary->artificial_int_max_size = 2 * defaults.min - 1;
    {
        hive_type small_hive(std::hive_limits{1, 1}, allocator_type(small, 1));
        ASSERT_NO_THROW(small_hive.emplace(41));
        EXPECT_EQ(small_hive.max_size(), 4u);

        hive_type hive{allocator_type(ordinary, 2)};
        EXPECT_EQ(hive.block_capacity_limits().min, defaults.min);
        EXPECT_EQ(hive.block_capacity_limits().max, defaults.max);
        EXPECT_EQ(hive.max_size(), 2 * defaults.min - 1);
        ASSERT_NO_THROW(hive.emplace(43));
        EXPECT_EQ(hive.capacity(), defaults.min);
        const auto allocations = ordinary->allocation_attempts;
        EXPECT_THROW(hive.reserve(defaults.min + 1), std::length_error);
        EXPECT_EQ(ordinary->allocation_attempts, allocations);
        EXPECT_EQ(hive.capacity(), defaults.min);
        EXPECT_EQ(*hive.begin(), 43);
        EXPECT_EQ(hive_type::block_capacity_hard_limits().max, hard.max);
    }
    EXPECT_TRUE(small->clean());
    EXPECT_TRUE(ordinary->clean());
}

TEST(HiveAllocator, PmrExplicitResourceRetainsDefaultGrowth) {
    using hive_type = std::pmr::hive<int>;
    constexpr auto hard = hive_type::block_capacity_hard_limits();
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    static_assert(hard.max == std::hive<int>::block_capacity_hard_limits().max);
    static_assert(defaults.min == std::hive<int>::block_capacity_default_limits().min);
    static_assert(defaults.max == std::hive<int>::block_capacity_default_limits().max);
    static_assert(noexcept(hive_type::block_capacity_hard_limits()));
    static_assert(noexcept(hive_type::block_capacity_default_limits()));
    alignas(std::max_align_t) std::byte buffer[4096];
    std::pmr::monotonic_buffer_resource resource(
        buffer, sizeof(buffer), std::pmr::null_memory_resource());
    hive_type hive{std::pmr::polymorphic_allocator<int>{&resource}};
    EXPECT_EQ(hive.get_allocator().resource(), &resource);
    EXPECT_EQ(hive.block_capacity_limits().min, defaults.min);
    for (std::size_t i = 0; i <= defaults.min; ++i) {
        ASSERT_NO_THROW(hive.emplace(static_cast<int>(i)));
    }
    EXPECT_EQ(hive.capacity(), 2 * defaults.min);
    EXPECT_EQ(hive.size(), defaults.min + 1);
}

} // namespace

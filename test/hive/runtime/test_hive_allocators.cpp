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

TEST(HiveAllocator, SelectOnContainerCopyConstructionAndExplicitAllocator) {
    using allocator_type = stateful_allocator<int>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 2, 3, 4};
    {
        const allocator_type allocator(state, 7);
        std::hive<int, allocator_type> source(std::from_range, input, allocator);
        std::hive<int, allocator_type> copy(source);
        EXPECT_EQ(copy.get_allocator().id(), 107);
        EXPECT_EQ(source.get_allocator().id(), 7);
        EXPECT_EQ(values(copy), input);

        std::hive<int, allocator_type> explicit_copy(source, allocator_type(state, 22));
        EXPECT_EQ(explicit_copy.get_allocator().id(), 22);
        EXPECT_EQ(values(explicit_copy), input);
    }
    EXPECT_TRUE(state->clean());
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

TEST(HiveAllocator, PoccaFalseKeepsDestinationAllocator) {
    check_copy_assignment_propagation<false>();
}

TEST(HiveAllocator, PoccaTrueReleasesWithOldAllocatorThenPropagates) {
    check_copy_assignment_propagation<true>();
}

TEST(HiveAllocator, PocmaTrueStealsAndPropagates) {
    using allocator_type = stateful_allocator<int, false, true>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 2, 3, 4};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::from_range, std::vector<int>{9, 9}, allocator_type(state, 20));
        auto migrated = std::next(source.begin());
        const int* address = std::addressof(*migrated);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 10);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(std::addressof(*migrated), address);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocmaFalseUnequalAllocatorUsesElementMovePath) {
    using allocator_type = stateful_allocator<int, false, false>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{2, 4, 6, 8, 10};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    const std::hive_limits destination_limits{defaults.min, defaults.max};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            destination_limits, allocator_type(state, 20));
        destination.emplace(-1);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.block_capacity_limits().min, destination_limits.min);
        EXPECT_EQ(destination.block_capacity_limits().max, destination_limits.max);
        EXPECT_GT(state->count_for_id(20), 0u);
        destination.emplace(12); // The unequal-move destination remains reusable.
        source.emplace(99);     // The moved-from source remains reusable.
        EXPECT_EQ(*source.begin(), 99);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocmaFalseEqualAllocatorStealsAndCopiesLimits) {
    using allocator_type = stateful_allocator<int, false, false>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{2, 3, 5, 7};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::hive_limits{defaults.min, defaults.max}, allocator_type(state, 10));
        auto migrated = source.begin();
        const int* address = std::addressof(*migrated);
        destination = std::move(source);
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 10);
        EXPECT_EQ(values(destination), input);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(destination.block_capacity_limits().max, source_limits.max);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, AlwaysEqualPermitsStealWithoutPocma) {
    using allocator_type = stateful_allocator<int, false, false, false, true>;
    static_assert(std::allocator_traits<allocator_type>::is_always_equal::value);
    static_assert(noexcept(
        std::declval<std::hive<int, allocator_type>&>() =
            std::declval<std::hive<int, allocator_type>&&>()));
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{4, 5, 6};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(allocator_type(state, 20));
        auto migrated = source.begin();
        const int* address = std::addressof(*migrated);
        ASSERT_EQ(source.get_allocator(), destination.get_allocator());
        destination = std::move(source);
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(destination.get_iterator(address), migrated);
        EXPECT_EQ(destination.block_capacity_limits().max, source_limits.max);
        EXPECT_EQ(values(destination), input);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, UnequalAllocatorMoveConstructorMovesElementsAndCopiesLimits) {
    using allocator_type = stateful_allocator<int>;
    auto state = std::make_shared<allocation_state>();
    const std::vector<int> input{1, 3, 5, 7};
    constexpr auto defaults = std::hive<int, allocator_type>::block_capacity_default_limits();
    const std::hive_limits source_limits{defaults.min, defaults.min};
    {
        std::hive<int, allocator_type> source(
            std::from_range, input, source_limits, allocator_type(state, 10));
        std::hive<int, allocator_type> destination(
            std::move(source), allocator_type(state, 20));
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(destination.get_allocator().id(), 20);
        EXPECT_EQ(values(destination), input);
        // The unequal-allocator move constructor moves elements; unlike the
        // steal path, the standard does not require copying current-limits.
        destination.emplace(9);
        source.emplace(11);
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocsTrueSwapsUnequalStatefulAllocators) {
    using allocator_type = stateful_allocator<int, false, false, true>;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<int, allocator_type> left(
            std::from_range, std::vector<int>{1, 2}, allocator_type(state, 10));
        std::hive<int, allocator_type> right(
            std::from_range, std::vector<int>{7, 8, 9}, allocator_type(state, 20));
        auto migrated = right.begin();
        const int* address = std::addressof(*migrated);
        left.swap(right);
        EXPECT_EQ(left.get_allocator().id(), 20);
        EXPECT_EQ(right.get_allocator().id(), 10);
        EXPECT_EQ(left.get_iterator(address), migrated);
        EXPECT_EQ(values(left), (std::vector<int>{7, 8, 9}));
        EXPECT_EQ(values(right), (std::vector<int>{1, 2}));
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveAllocator, PocsFalseEqualAllocatorSwapKeepsAllocatorAndAddresses) {
    using allocator_type = stateful_allocator<int, false, false, false>;
    auto state = std::make_shared<allocation_state>();
    {
        std::hive<int, allocator_type> left(
            std::from_range, std::vector<int>{1, 2}, allocator_type(state, 10));
        std::hive<int, allocator_type> right(
            std::from_range, std::vector<int>{7, 8, 9}, allocator_type(state, 10));
        auto migrated = right.begin();
        const int* address = std::addressof(*migrated);
        left.swap(right);
        EXPECT_EQ(left.get_allocator().id(), 10);
        EXPECT_EQ(right.get_allocator().id(), 10);
        EXPECT_EQ(left.get_iterator(address), migrated);
        EXPECT_EQ(values(left), (std::vector<int>{7, 8, 9}));
        EXPECT_EQ(values(right), (std::vector<int>{1, 2}));
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

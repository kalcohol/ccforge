#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace {

using simd_test::int4;
using native_int = std::simd::basic_vec<int>;
constexpr std::size_t extent = native_int::size > 4 ? native_int::size : 4;

template<std::size_t N>
struct fixed_view {
    int* first;

    explicit constexpr fixed_view(int* p) noexcept : first(p) {}
    fixed_view() = delete;
    fixed_view(const fixed_view&) = delete;
    fixed_view(fixed_view&&) = delete;

    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() const noexcept { return first + N; }
    constexpr std::size_t size() const noexcept { return N; }
};

struct const_longer_view {
    int* first;

    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() noexcept { return first + 2; }
    constexpr int* end() const noexcept { return first + 4; }
    constexpr std::size_t size() noexcept { return 2; }
    constexpr std::size_t size() const noexcept { return 4; }
};

struct dynamic_view {
    int* first;
    std::size_t count;
    int* size_queries;

    dynamic_view(int* p, std::size_t n, int& queries) noexcept
        : first(p), count(n), size_queries(&queries) {}
    dynamic_view(const dynamic_view&) = delete;
    dynamic_view(dynamic_view&&) = delete;

    int* begin() const noexcept { return first; }
    int* end() const noexcept { return first + count; }
    std::size_t size() const noexcept {
        ++*size_queries;
        return count;
    }
};

struct stateful_constexpr_view {
    int* first;
    std::size_t count;

    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() const noexcept { return first + count; }
    constexpr std::size_t size() const noexcept { return count; }
};

struct derived_array : std::array<int, extent> {};

static_assert(std::ranges::contiguous_range<fixed_view<extent>>);
static_assert(std::ranges::sized_range<fixed_view<extent>>);
static_assert(std::ranges::contiguous_range<dynamic_view>);
static_assert(std::ranges::sized_range<dynamic_view>);

template<class R>
concept unchecked_declarations = requires(R&& r, const int4& value,
                                         const int4::mask_type& mask,
                                         const native_int::mask_type& native_mask) {
    { std::simd::unchecked_load<int4>(std::forward<R>(r)) } -> std::same_as<int4>;
    { std::simd::unchecked_load<int4, R>(std::forward<R>(r), mask) } -> std::same_as<int4>;
    { std::simd::unchecked_load(std::forward<R>(r)) } -> std::same_as<native_int>;
    { std::simd::unchecked_load<R>(std::forward<R>(r), native_mask) } -> std::same_as<native_int>;
    { std::simd::unchecked_store(value, std::forward<R>(r)) } -> std::same_as<void>;
    { std::simd::unchecked_store(value, std::forward<R>(r), mask) } -> std::same_as<void>;
};

// Static-short ranges violate Mandates on instantiation, not declaration constraints.
static_assert(unchecked_declarations<int (&)[2]>);
static_assert(unchecked_declarations<std::array<int, 2>&>);
static_assert(unchecked_declarations<std::span<int, 2>>);
static_assert(unchecked_declarations<fixed_view<2>&>);

template<class V>
constexpr bool expected_load(const V& value, bool masked) {
    for (std::simd::simd_size_type i = 0; i < V::size; ++i) {
        const int expected = !masked || i % 2 == 0 ? 10 + static_cast<int>(i) : 0;
        if (value[i] != expected) {
            return false;
        }
    }
    return true;
}

template<class R>
constexpr bool check_loads(R&& input) {
    const int4::mask_type mask(std::bitset<4>(0b0101));
    const native_int::mask_type native_mask([](auto i) { return i % 2 == 0; });
    // Name R for masked loads so this fixture does not depend on the separate array ambiguity fix.
    return expected_load(std::simd::unchecked_load<int4>(std::forward<R>(input)), false) &&
        expected_load(std::simd::unchecked_load<int4>(std::forward<R>(input), std::simd::flag_default), false) &&
        expected_load(std::simd::unchecked_load<int4, R>(std::forward<R>(input), mask), true) &&
        expected_load(std::simd::unchecked_load<int4, R>(
            std::forward<R>(input), mask, std::simd::flag_default), true) &&
        expected_load(std::simd::unchecked_load(std::forward<R>(input)), false) &&
        expected_load(std::simd::unchecked_load(std::forward<R>(input), std::simd::flag_default), false) &&
        expected_load(std::simd::unchecked_load<R>(std::forward<R>(input), native_mask), true) &&
        expected_load(std::simd::unchecked_load<R>(
            std::forward<R>(input), native_mask, std::simd::flag_default), true);
}

template<class R>
constexpr bool check_stores(R&& output) {
    const int4 value([](auto i) { return 10 + static_cast<int>(i); });
    const int4::mask_type mask(std::bitset<4>(0b0101));
    for (int& item : output) {
        item = -1;
    }
    std::simd::unchecked_store(value, std::forward<R>(output), std::simd::flag_default);
    for (std::size_t i = 0; i < std::ranges::size(output); ++i) {
        if (std::ranges::data(output)[i] != (i < 4 ? 10 + static_cast<int>(i) : -1)) {
            return false;
        }
    }
    for (int& item : output) {
        item = -1;
    }
    std::simd::unchecked_store(value, std::forward<R>(output), mask);
    for (std::size_t i = 0; i < std::ranges::size(output); ++i) {
        if (std::ranges::data(output)[i] != (i < 4 && i % 2 == 0 ? 10 + static_cast<int>(i) : -1)) {
            return false;
        }
    }
    return true;
}

constexpr bool static_ranges_are_constexpr() {
    int input[extent]{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    fixed_view<extent> fixed(input);
    stateful_constexpr_view stateful{input, extent};
    const const_longer_view cv_view{input};
    const auto& constant = input;
    return check_loads(input) && check_loads(constant) && check_loads(std::move(constant)) &&
        check_loads(fixed) && check_loads(std::move(fixed)) && check_loads(stateful) &&
        expected_load(std::simd::unchecked_load<int4>(cv_view), false) &&
        check_stores(input) && check_stores(fixed) && check_stores(std::move(fixed)) &&
        check_stores(cv_view);
}

static_assert(static_ranges_are_constexpr());

TEST(SimdMemoryStaticExtent, ArraysSpansAndNoncopyableStaticRanges) {
    std::array<int, extent> input{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    derived_array derived{};
    for (std::size_t i = 0; i < extent; ++i) {
        derived[i] = input[i];
    }
    fixed_view<extent> fixed(input.data());
    const std::span<int, extent> mutable_span(input);
    const std::span<const int, extent> readonly_span(input);
    EXPECT_TRUE(check_loads(input));
    EXPECT_TRUE(check_loads(std::as_const(input)));
    EXPECT_TRUE(check_loads(std::move(input)));
    EXPECT_TRUE(check_loads(mutable_span));
    EXPECT_TRUE(check_loads(readonly_span));
    EXPECT_TRUE(check_loads(derived));
    EXPECT_TRUE(check_loads(std::as_const(derived)));
    EXPECT_TRUE(check_loads(fixed));
    EXPECT_TRUE(check_loads(std::move(fixed)));
    EXPECT_TRUE(check_stores(input));
    EXPECT_TRUE(check_stores(mutable_span));
    EXPECT_TRUE(check_stores(derived));
    EXPECT_TRUE(check_stores(fixed));
}

TEST(SimdMemoryStaticExtent, ExactWidthAndCvDependentStaticSizes) {
    int input[]{10, 11, 12, 13};
    const const_longer_view cv_view{input};
    fixed_view<4> fixed(input);
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(input), false));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(cv_view), false));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(fixed), false));
    const int4::mask_type none(false);
    const auto empty = std::simd::unchecked_load<int4>(std::span<const int, 4>(input), none);
    std::simd::unchecked_store(int4(7), cv_view, none);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        EXPECT_EQ(empty[i], 0);
        EXPECT_EQ(input[i], 10 + static_cast<int>(i));
    }
    EXPECT_TRUE(check_stores(std::span<int, 4>(input)));
    EXPECT_TRUE(check_stores(cv_view));
}

TEST(SimdMemoryStaticExtent, DynamicExtentRetainsRuntimeSizeQueriesWithoutCopies) {
    std::vector<int> input(extent);
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    int queries = 0;
    dynamic_view view(input.data(), input.size(), queries);
    const int4::mask_type mask(std::bitset<4>(0b0101));
    const native_int::mask_type native_mask([](auto i) { return i % 2 == 0; });
    EXPECT_TRUE(expected_load(std::simd::unchecked_load(view), false));
    EXPECT_EQ(std::exchange(queries, 0), 2);
    EXPECT_TRUE(expected_load(std::simd::unchecked_load(view, native_mask), true));
    EXPECT_EQ(std::exchange(queries, 0), 2);
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(std::move(view)), false));
    EXPECT_EQ(std::exchange(queries, 0), 1);
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(view, mask), true));
    EXPECT_EQ(std::exchange(queries, 0), 1);
    const int4 value(7);
    std::simd::unchecked_store(value, view);
    EXPECT_EQ(std::exchange(queries, 0), 1);
    std::simd::unchecked_store(value, std::move(view), mask);
    EXPECT_EQ(std::exchange(queries, 0), 1);
    EXPECT_TRUE(check_stores(input));
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    EXPECT_TRUE(check_loads(std::span<int>(input)));
}

TEST(SimdMemoryStaticExtent, PointerCountAndSentinelExtensionsRemainUsable) {
    int input[extent]{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    int* const pointer = input;
    const int* const const_pointer = input;
    const int4::mask_type mask(std::bitset<4>(0b0101));
    const native_int::mask_type native_mask([](auto i) { return i % 2 == 0; });
    EXPECT_TRUE(expected_load(std::simd::unchecked_load(pointer), false));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load(const_pointer, native_mask), true));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(const_pointer), false));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(pointer, mask), true));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(pointer, extent), false));
    EXPECT_TRUE(expected_load(std::simd::unchecked_load<int4>(pointer, pointer + extent, mask), true));
    const int4 value(7);
    std::simd::unchecked_store(value, pointer);
    std::simd::unchecked_store(value, pointer, mask);
    std::simd::unchecked_store(value, pointer, extent, mask);
    std::simd::unchecked_store(value, pointer, pointer + extent);
    for (std::size_t i = 0; i < extent; ++i) {
        EXPECT_EQ(input[i], i < 4 ? 7 : 10 + static_cast<int>(i));
    }
}

TEST(SimdMemoryStaticExtent, PartialShortRangesKeepTheirDefinedBehavior) {
    int input[]{10, 11};
    const auto unmasked = std::simd::partial_load<int4>(input);
    const std::array<int, 2> short_array{10, 11};
    const auto array_value = std::simd::partial_load<int4>(short_array);
    const int4::mask_type second(std::bitset<4>(0b0010));
    const auto masked = std::simd::partial_load<int4>(std::span<const int, 2>(input), second);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        EXPECT_EQ(unmasked[i], i < 2 ? 10 + static_cast<int>(i) : 0);
        EXPECT_EQ(array_value[i], unmasked[i]);
        EXPECT_EQ(masked[i], i == 1 ? 11 : 0);
    }
    struct {
        int output[2]{-1, -1};
        int guard = 99;
    } destination;
    std::simd::partial_store(int4(7), destination.output);
    EXPECT_EQ(destination.output[0], 7);
    EXPECT_EQ(destination.output[1], 7);
    EXPECT_EQ(destination.guard, 99);
    std::simd::partial_store(int4(8), std::span<int, 2>(destination.output), second);
    EXPECT_EQ(destination.output[0], 7);
    EXPECT_EQ(destination.output[1], 8);
    EXPECT_EQ(destination.guard, 99);
    fixed_view<2> short_view(input);
    EXPECT_EQ(std::simd::partial_load<int4>(short_view)[2], 0);
    std::simd::partial_store(int4(9), short_view, second);
    EXPECT_EQ(input[0], 10);
    EXPECT_EQ(input[1], 9);
}

TEST(SimdMemoryStaticExtent, GatherScatterDoNotRequireAFullWidthRange) {
    const std::array<int, 1> input{42};
    const int4 indices(0);
    const auto gathered = std::simd::unchecked_gather_from<int4>(input, indices);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        EXPECT_EQ(gathered[i], 42);
    }
    std::array<int, 1> output{-1};
    const int4::mask_type first(std::bitset<4>(0b0001));
    std::simd::unchecked_scatter_to(int4(17), std::span<int, 1>(output), first, indices);
    EXPECT_EQ(output[0], 17);
}

} // namespace

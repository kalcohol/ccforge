#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <span>
#include <utility>

namespace {

using simd_test::int4;
using native_int = std::simd::basic_vec<int>;
constexpr std::size_t extent = native_int::size > 4 ? native_int::size : 4;

template<class R>
concept masked_load_forms = requires(R&& input, const int4::mask_type& mask,
                                    const native_int::mask_type& native_mask) {
    { std::simd::unchecked_load<int4>(std::forward<R>(input), mask) } -> std::same_as<int4>;
    { std::simd::unchecked_load<int4>(std::forward<R>(input), mask, std::simd::flag_default) }
        -> std::same_as<int4>;
    { std::simd::unchecked_load(std::forward<R>(input), native_mask) } -> std::same_as<native_int>;
    { std::simd::unchecked_load(std::forward<R>(input), native_mask, std::simd::flag_default) }
        -> std::same_as<native_int>;
};

static_assert(masked_load_forms<int (&)[extent]>);
static_assert(masked_load_forms<const int (&)[extent]>);
static_assert(masked_load_forms<int (&&)[extent]>);
static_assert(masked_load_forms<const int (&&)[extent]>);
static_assert(masked_load_forms<int*>);
static_assert(masked_load_forms<int* const&>);
static_assert(masked_load_forms<const int*>);
static_assert(masked_load_forms<const int* const&>);
static_assert(masked_load_forms<const std::array<int, extent>&>);
static_assert(masked_load_forms<std::span<const int, extent>>);
static_assert(masked_load_forms<const std::span<int, extent>&>);

template<class R>
constexpr bool check_masked_loads(R&& input) {
    const int4::mask_type mask(std::bitset<4>(0b0101));
    const native_int::mask_type native_mask([](auto i) { return i % 2 == 0; });
    const auto explicit_value = std::simd::unchecked_load<int4>(std::forward<R>(input), mask);
    const auto explicit_flags = std::simd::unchecked_load<int4>(
        std::forward<R>(input), mask, std::simd::flag_default);
    const auto deduced_value = std::simd::unchecked_load(std::forward<R>(input), native_mask);
    const auto deduced_flags = std::simd::unchecked_load(
        std::forward<R>(input), native_mask, std::simd::flag_default);
    for (std::simd::simd_size_type i = 0; i < native_int::size; ++i) {
        const int expected = i % 2 == 0 ? 10 + static_cast<int>(i) : 0;
        if (deduced_value[i] != expected || deduced_flags[i] != expected) {
            return false;
        }
    }
    for (std::simd::simd_size_type i = 0; i < int4::size; ++i) {
        const int expected = i % 2 == 0 ? 10 + static_cast<int>(i) : 0;
        if (explicit_value[i] != expected || explicit_flags[i] != expected) {
            return false;
        }
    }
    return true;
}

constexpr bool arrays_are_constexpr() {
    int input[extent]{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    const auto& constant = input;
    return check_masked_loads(input) && check_masked_loads(constant) &&
        check_masked_loads(std::move(input)) && check_masked_loads(std::move(constant));
}

static_assert(arrays_are_constexpr());

TEST(SimdMaskedArrayLoad, BuiltInArrayCvrefFormsUseTheRangeOverloads) {
    int input[extent]{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    const auto& constant = input;
    EXPECT_TRUE(check_masked_loads(input));
    EXPECT_TRUE(check_masked_loads(constant));
    EXPECT_TRUE(check_masked_loads(std::move(input)));
    EXPECT_TRUE(check_masked_loads(std::move(constant)));
}

TEST(SimdMaskedArrayLoad, PointerAndRangeControlsRetainTheirValues) {
    std::array<int, extent> input{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    int* const pointer = input.data();
    const int* const constant_pointer = input.data();
    const std::span<int, extent> mutable_view(input);
    const std::span<const int, extent> readonly_view(input);
    EXPECT_TRUE(check_masked_loads(pointer));
    EXPECT_TRUE(check_masked_loads(constant_pointer));
    EXPECT_TRUE(check_masked_loads(input));
    EXPECT_TRUE(check_masked_loads(std::as_const(input)));
    EXPECT_TRUE(check_masked_loads(mutable_view));
    EXPECT_TRUE(check_masked_loads(readonly_view));
}

} // namespace

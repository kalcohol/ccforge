#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <bitset>
#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

template<class V>
concept has_min = requires(V&& value) {
    std::simd::reduce_min(std::forward<V>(value));
};

template<class V>
concept has_max = requires(V&& value) {
    std::simd::reduce_max(std::forward<V>(value));
};

template<class V>
concept has_masked_min = requires(V&& value, const typename std::remove_cvref_t<V>::mask_type& mask) {
    std::simd::reduce_min(std::forward<V>(value), mask);
};

template<class V>
concept has_masked_max = requires(V&& value, const typename std::remove_cvref_t<V>::mask_type& mask) {
    std::simd::reduce_max(std::forward<V>(value), mask);
};

template<class V>
constexpr bool ordered_vector_forms = has_min<V> && has_max<V> &&
    has_masked_min<V> && has_masked_max<V>;

static_assert(ordered_vector_forms<simd_test::int4>);
static_assert(ordered_vector_forms<const simd_test::int4&>);
static_assert(ordered_vector_forms<simd_test::float4&>);
static_assert(ordered_vector_forms<const simd_test::double4&&>);
static_assert(!has_min<simd_test::complex4f>);
static_assert(!has_max<simd_test::complex4f>);
static_assert(!has_masked_min<simd_test::complex4f>);
static_assert(!has_masked_max<simd_test::complex4f>);
static_assert(!has_min<const simd_test::complex4d&>);
static_assert(!has_max<const simd_test::complex4d&>);
static_assert(!has_masked_min<const simd_test::complex4d&>);
static_assert(!has_masked_max<const simd_test::complex4d&>);

using simd_test::int4;
static_assert(std::same_as<decltype(std::simd::reduce_min(std::declval<const int4&>())), int>);
static_assert(std::same_as<decltype(std::simd::reduce_max(std::declval<const int4&>())), int>);
static_assert(noexcept(std::simd::reduce_min(std::declval<const int4&>())));
static_assert(noexcept(std::simd::reduce_max(std::declval<const int4&>())));
static_assert(noexcept(std::simd::reduce_min(
    std::declval<const int4&>(), std::declval<const int4::mask_type&>())));
static_assert(noexcept(std::simd::reduce_max(
    std::declval<const int4&>(), std::declval<const int4::mask_type&>())));

constexpr bool ordered_reductions_are_constexpr() {
    const int4 value([](auto i) { return 3 * static_cast<int>(i) - 5; });
    const int4::mask_type selected(std::bitset<4>(0b0101));
    const int4::mask_type none(false);
    return std::simd::reduce_min(value) == -5 &&
        std::simd::reduce_max(value) == 4 &&
        std::simd::reduce_min(value, selected) == -5 &&
        std::simd::reduce_max(value, selected) == 1 &&
        std::simd::reduce_min(value, none) == std::numeric_limits<int>::max() &&
        std::simd::reduce_max(value, none) == std::numeric_limits<int>::lowest();
}

static_assert(ordered_reductions_are_constexpr());

TEST(SimdReduceOrderConstraints, OrderedVectorValuesAndEmptyMasks) {
    EXPECT_TRUE(ordered_reductions_are_constexpr());
    const simd_test::double4 value([](auto i) { return 3.0 * static_cast<int>(i) - 5.0; });
    const simd_test::double4::mask_type none(false);
    EXPECT_EQ(std::simd::reduce_min(value), -5.0);
    EXPECT_EQ(std::simd::reduce_max(std::move(value)), 4.0);
    EXPECT_EQ(std::simd::reduce_min(value, none), std::numeric_limits<double>::max());
    EXPECT_EQ(std::simd::reduce_max(value, none), std::numeric_limits<double>::lowest());
}

TEST(SimdReduceOrderConstraints, OrderedMaskedValuesPreserveSignedZero) {
    const simd_test::float4 value([](auto i) {
        constexpr float lanes[]{-0.0f, 2.0f, -1.0f, 3.0f};
        return lanes[static_cast<int>(i)];
    });
    const simd_test::float4::mask_type selected(std::bitset<4>(0b0101));
    EXPECT_EQ(std::simd::reduce_min(value), -1.0f);
    EXPECT_EQ(std::simd::reduce_max(value), 3.0f);
    EXPECT_EQ(std::simd::reduce_min(value, selected), -1.0f);
    EXPECT_TRUE(std::signbit(std::simd::reduce_max(value, selected)));
    EXPECT_EQ(std::simd::reduce_min(7), 7);
    EXPECT_EQ(std::simd::reduce_max(7, false), std::numeric_limits<int>::lowest());
}

} // namespace

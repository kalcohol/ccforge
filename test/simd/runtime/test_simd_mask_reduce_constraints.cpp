#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <bitset>
#include <concepts>
#include <type_traits>
#include <utility>

namespace {

template<class T>
constexpr int mask_reduction_forms =
    int(requires(T&& value) { std::simd::all_of(std::forward<T>(value)); }) +
    int(requires(T&& value) { std::simd::any_of(std::forward<T>(value)); }) +
    int(requires(T&& value) { std::simd::none_of(std::forward<T>(value)); }) +
    int(requires(T&& value) { std::simd::reduce_count(std::forward<T>(value)); }) +
    int(requires(T&& value) { std::simd::reduce_min_index(std::forward<T>(value)); }) +
    int(requires(T&& value) { std::simd::reduce_max_index(std::forward<T>(value)); });

struct implicit_bool {
    constexpr operator bool() const noexcept { return true; }
};

struct explicit_bool {
    explicit constexpr operator bool() const noexcept { return true; }
};

enum boolean_enum { false_value, true_value };

static_assert(mask_reduction_forms<bool> == 6);
static_assert(mask_reduction_forms<bool&> == 6);
static_assert(mask_reduction_forms<const bool&> == 6);
static_assert(mask_reduction_forms<const bool&&> == 6);
static_assert(mask_reduction_forms<volatile bool&> == 6);
static_assert(mask_reduction_forms<const simd_test::mask4&> == 6);
static_assert(mask_reduction_forms<int> == 0);
static_assert(mask_reduction_forms<double> == 0);
static_assert(mask_reduction_forms<int*> == 0);
static_assert(mask_reduction_forms<boolean_enum> == 0);
static_assert(mask_reduction_forms<implicit_bool> == 0);
static_assert(mask_reduction_forms<const implicit_bool&> == 0);
static_assert(mask_reduction_forms<explicit_bool> == 0);
static_assert(mask_reduction_forms<std::true_type> == 0);

static_assert(std::same_as<decltype(std::simd::all_of(true)), bool>);
static_assert(std::same_as<decltype(std::simd::any_of(true)), bool>);
static_assert(std::same_as<decltype(std::simd::none_of(true)), bool>);
static_assert(std::same_as<decltype(std::simd::reduce_count(true)), std::simd::simd_size_type>);
static_assert(std::same_as<decltype(std::simd::reduce_min_index(true)), std::simd::simd_size_type>);
static_assert(std::same_as<decltype(std::simd::reduce_max_index(true)), std::simd::simd_size_type>);
static_assert(noexcept(std::simd::all_of(true)));
static_assert(noexcept(std::simd::any_of(true)));
static_assert(noexcept(std::simd::none_of(true)));
static_assert(noexcept(std::simd::reduce_count(true)));
static_assert(noexcept(std::simd::reduce_min_index(true)));
static_assert(noexcept(std::simd::reduce_max_index(true)));

constexpr bool (*all_bool)(bool) noexcept = std::simd::all_of;
constexpr bool (*any_bool)(bool) noexcept = std::simd::any_of;
constexpr bool (*none_bool)(bool) noexcept = std::simd::none_of;

constexpr bool boolean_reductions_are_constexpr() {
    return all_bool(true) && !all_bool(false) && any_bool(true) && !any_bool(false) &&
        !none_bool(true) && none_bool(false) &&
        std::simd::reduce_count(true) == 1 && std::simd::reduce_count(false) == 0 &&
        std::simd::reduce_min_index(true) == 0 && std::simd::reduce_max_index(true) == 0;
}

static_assert(boolean_reductions_are_constexpr());

TEST(SimdMaskReduceConstraints, ExactBooleanValuesPreserveTheirResults) {
    EXPECT_TRUE(boolean_reductions_are_constexpr());
    const bool selected = true;
    volatile bool unselected = false;
    EXPECT_TRUE(std::simd::all_of(selected));
    EXPECT_TRUE(std::simd::any_of(std::move(selected)));
    EXPECT_TRUE(std::simd::none_of(unselected));
    EXPECT_EQ(std::simd::reduce_count(unselected), 0);
    EXPECT_EQ(std::simd::reduce_min_index(selected), 0);
    EXPECT_EQ(std::simd::reduce_max_index(selected), 0);
}

TEST(SimdMaskReduceConstraints, VectorMaskOverloadsRemainAvailable) {
    const simd_test::mask4 selected(std::bitset<4>(0b0101));
    const simd_test::mask4 all(true);
    const simd_test::mask4 none(false);
    EXPECT_FALSE(std::simd::all_of(selected));
    EXPECT_TRUE(std::simd::any_of(selected));
    EXPECT_FALSE(std::simd::none_of(selected));
    EXPECT_EQ(std::simd::reduce_count(selected), 2);
    EXPECT_EQ(std::simd::reduce_min_index(selected), 0);
    EXPECT_EQ(std::simd::reduce_max_index(selected), 2);
    EXPECT_TRUE(std::simd::all_of(all));
    EXPECT_TRUE(std::simd::none_of(none));
}

TEST(SimdMaskReduceConstraints, ExplicitCallerNormalizationRemainsValid) {
    const bool normalized = static_cast<bool>(implicit_bool{});
    EXPECT_TRUE(std::simd::all_of(normalized));
    EXPECT_EQ(std::simd::reduce_count(normalized), 1);
    EXPECT_EQ(std::simd::reduce_min_index(normalized), 0);
    EXPECT_EQ(std::simd::reduce_max_index(normalized), 0);
}

} // namespace

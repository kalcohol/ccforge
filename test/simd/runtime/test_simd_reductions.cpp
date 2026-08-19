#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>

namespace {

using namespace simd_test;

struct simd_plus {
    template<class T, class Abi>
    constexpr std::simd::basic_vec<T, Abi> operator()(const std::simd::basic_vec<T, Abi>& left,
                                                      const std::simd::basic_vec<T, Abi>& right) const noexcept {
        return left + right;
    }
};

TEST(SimdRuntimeTest, ReduceFamilyProducesExpectedValues) {
    const std::array<int, 4> data{{1, 4, 2, 3}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);
    const mask4 none_selected(0u);

    EXPECT_EQ(std::simd::reduce(values), 10);
    EXPECT_EQ(std::simd::reduce(values, selected), 3);
    EXPECT_EQ(std::simd::reduce_min(values), 1);
    EXPECT_EQ(std::simd::reduce_min(values, selected), 1);
    EXPECT_EQ(std::simd::reduce_min(values, none_selected), std::numeric_limits<int>::max());
    EXPECT_EQ(std::simd::reduce_max(values), 4);
    EXPECT_EQ(std::simd::reduce_max(values, selected), 2);
    EXPECT_EQ(std::simd::reduce_max(values, none_selected), std::numeric_limits<int>::lowest());
}

TEST(SimdRuntimeTest, ReduceAcceptsVecOneReducerModel) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);

    EXPECT_EQ(std::simd::reduce(values, simd_plus{}), 10);
}

TEST(SimdRuntimeTest, MaskedBitwiseReduceUsesStandardDefaultIdentities) {
    const std::array<int, 4> data{{0b1111, 0b1100, 0b1010, 0b1001}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);

    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_and<>{}), 0b1010);
    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_or<>{}), 0b1111);
    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_xor<>{}), 0b0101);
}

TEST(SimdRuntimeTest, NonemptyMaskedReduceStartsFromTheFirstSelectedValue) {
    const std::array<float, 4> data{{-0.0f, 1.0f, 2.0f, 3.0f}};
    const float4 values = load_vec<float4>(data);
    const float4::mask_type first_lane(0b0001u);

    EXPECT_TRUE(std::signbit(std::simd::reduce(values, first_lane)));
    EXPECT_TRUE(std::signbit(std::simd::reduce(values, first_lane, simd_plus{}, 0.0f)));
}

TEST(SimdRuntimeTest, EmptyMaskedReduceReturnsTheIdentityElement) {
    const std::array<float, 4> data{{-0.0f, 1.0f, 2.0f, 3.0f}};
    const float4 values = load_vec<float4>(data);
    const float4::mask_type no_lanes(0u);
    const float identity = -7.0f;

    EXPECT_EQ(std::simd::reduce(values, no_lanes, std::plus<>{}, identity), identity);
}

// The identity parameter is a non-deduced type_identity_t<T> per the WD, so
// an int literal identity for a float vec must convert instead of fighting
// template argument deduction.
TEST(SimdRuntimeTest, MaskedReduceIdentityParameterDoesNotParticipateInDeduction) {
    const std::array<float, 4> data{{1.0f, 2.0f, 3.0f, 4.0f}};
    const float4 values = load_vec<float4>(data);
    const float4::mask_type no_lanes(0u);
    const float4::mask_type all_lanes(0b1111u);

    EXPECT_EQ(std::simd::reduce(values, no_lanes, std::plus<>{}, 0), 0.0f);
    EXPECT_EQ(std::simd::reduce(values, all_lanes, std::plus<>{}, 0), 10.0f);
}

TEST(SimdRuntimeTest, ScalarReductionsMirrorVectorGenericCode) {
    EXPECT_EQ(std::simd::reduce(7), 7);
    EXPECT_EQ(std::simd::reduce(7, true), 7);
    EXPECT_EQ(std::simd::reduce(7, false), 0);
    EXPECT_EQ(std::simd::reduce(7, false, std::multiplies<>{}), 1);
    EXPECT_EQ(std::simd::reduce_min(7), 7);
    EXPECT_EQ(std::simd::reduce_min(7, true), 7);
    EXPECT_EQ(std::simd::reduce_min(7, false), std::numeric_limits<int>::max());
    EXPECT_EQ(std::simd::reduce_max(7), 7);
    EXPECT_EQ(std::simd::reduce_max(7, true), 7);
    EXPECT_EQ(std::simd::reduce_max(7, false), std::numeric_limits<int>::lowest());
}

} // namespace

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace simd_test;

TEST(SimdBitTest, UnaryBitAlgorithmsApplyPerLane) {
    const std::array<unsigned, 4> data{{0x12345678u, 0x0000000fu, 0x80000000u, 0x00000001u}};
    const uint4 values = load_vec<uint4>(data);

    const auto swapped = std::simd::byteswap(values);
    const auto counted = std::simd::popcount(values);
    const auto leading_zero = std::simd::countl_zero(values);
    const auto leading_one = std::simd::countl_one(values);
    const auto trailing_zero = std::simd::countr_zero(values);
    const auto trailing_one = std::simd::countr_one(values);
    const auto width = std::simd::bit_width(values);
    const auto single = std::simd::has_single_bit(values);
    const auto floored = std::simd::bit_floor(values);
    const auto ceiled = std::simd::bit_ceil(values);

    EXPECT_EQ(swapped[0], 0x78563412u);
    EXPECT_EQ(swapped[1], 0x0f000000u);
    EXPECT_EQ(swapped[2], 0x00000080u);
    EXPECT_EQ(swapped[3], 0x01000000u);

    EXPECT_EQ(counted[0], 13);
    EXPECT_EQ(counted[1], 4);
    EXPECT_EQ(counted[2], 1);
    EXPECT_EQ(counted[3], 1);

    EXPECT_EQ(leading_zero[0], 3);
    EXPECT_EQ(leading_zero[1], 28);
    EXPECT_EQ(leading_zero[2], 0);
    EXPECT_EQ(leading_zero[3], 31);

    EXPECT_EQ(leading_one[0], 0);
    EXPECT_EQ(leading_one[1], 0);
    EXPECT_EQ(leading_one[2], 1);
    EXPECT_EQ(leading_one[3], 0);

    EXPECT_EQ(trailing_zero[0], 3);
    EXPECT_EQ(trailing_zero[1], 0);
    EXPECT_EQ(trailing_zero[2], 31);
    EXPECT_EQ(trailing_zero[3], 0);

    EXPECT_EQ(trailing_one[0], 0);
    EXPECT_EQ(trailing_one[1], 4);
    EXPECT_EQ(trailing_one[2], 0);
    EXPECT_EQ(trailing_one[3], 1);

    EXPECT_EQ(width[0], 29);
    EXPECT_EQ(width[1], 4);
    EXPECT_EQ(width[2], 32);
    EXPECT_EQ(width[3], 1);

    EXPECT_FALSE(single[0]);
    EXPECT_FALSE(single[1]);
    EXPECT_TRUE(single[2]);
    EXPECT_TRUE(single[3]);

    EXPECT_EQ(floored[0], 0x10000000u);
    EXPECT_EQ(floored[1], 0x00000008u);
    EXPECT_EQ(floored[2], 0x80000000u);
    EXPECT_EQ(floored[3], 0x00000001u);

    EXPECT_EQ(ceiled[0], 0x20000000u);
    EXPECT_EQ(ceiled[1], 0x00000010u);
    EXPECT_EQ(ceiled[2], 0x80000000u);
    EXPECT_EQ(ceiled[3], 0x00000001u);
}

TEST(SimdBitTest, RotateAlgorithmsSupportScalarAndVectorCounts) {
    const uint4 values = load_vec<uint4>(std::array<unsigned, 4>{{1u, 2u, 4u, 8u}});
    const uint4 shifts = load_vec<uint4>(std::array<unsigned, 4>{{0u, 1u, 2u, 3u}});
    const uint4 right_values = load_vec<uint4>(std::array<unsigned, 4>{{16u, 8u, 32u, 64u}});
    const uint4 right_shifts = load_vec<uint4>(std::array<unsigned, 4>{{2u, 1u, 3u, 4u}});

    const auto scalar_left = std::simd::rotl(values, 1);
    const auto vector_left = std::simd::rotl(values, shifts);
    const auto scalar_right = std::simd::rotr(right_values, 1);
    const auto vector_right = std::simd::rotr(right_values, right_shifts);

    EXPECT_EQ(scalar_left[0], 2u);
    EXPECT_EQ(scalar_left[1], 4u);
    EXPECT_EQ(scalar_left[2], 8u);
    EXPECT_EQ(scalar_left[3], 16u);

    EXPECT_EQ(vector_left[0], 1u);
    EXPECT_EQ(vector_left[1], 4u);
    EXPECT_EQ(vector_left[2], 16u);
    EXPECT_EQ(vector_left[3], 64u);

    EXPECT_EQ(scalar_right[0], 8u);
    EXPECT_EQ(scalar_right[1], 4u);
    EXPECT_EQ(scalar_right[2], 16u);
    EXPECT_EQ(scalar_right[3], 32u);

    EXPECT_EQ(vector_right[0], 4u);
    EXPECT_EQ(vector_right[1], 4u);
    EXPECT_EQ(vector_right[2], 4u);
    EXPECT_EQ(vector_right[3], 4u);
}

} // namespace

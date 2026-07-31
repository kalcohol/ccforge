#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace simd_test;

TEST(SimdBitTest, UnaryBitAlgorithmsApplyPerLane) {
    const std::array<unsigned, 4> data{{0x12345678u, 0x0000000fu, 0x80000000u, 0x00000001u}};
    const uint4 values = load_vec<uint4>(data);

    const auto swapped = std::simd::byteswap(values);
    const auto reversed = std::simd::bit_reverse(values);
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

    EXPECT_EQ(reversed[0], 0x1e6a2c48u);
    EXPECT_EQ(reversed[1], 0xf0000000u);
    EXPECT_EQ(reversed[2], 0x00000001u);
    EXPECT_EQ(reversed[3], 0x80000000u);

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

TEST(SimdBitTest, CountAlgorithmsUseSignedReboundTypeForSmallUnsignedLanes) {
    const uchar4 values = load_vec<uchar4>(std::array<unsigned char, 4>{{0xffu, 0x0fu, 0x80u, 0x01u}});

    const auto counted = std::simd::popcount(values);
    const auto leading_zero = std::simd::countl_zero(values);
    const auto trailing_zero = std::simd::countr_zero(values);
    const auto width = std::simd::bit_width(values);

    static_assert(std::is_same_v<std::remove_cv_t<decltype(counted)>, schar4>);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(leading_zero)>, schar4>);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(trailing_zero)>, schar4>);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(width)>, schar4>);

    EXPECT_EQ(counted[0], 8);
    EXPECT_EQ(counted[1], 4);
    EXPECT_EQ(counted[2], 1);
    EXPECT_EQ(counted[3], 1);

    EXPECT_EQ(leading_zero[0], 0);
    EXPECT_EQ(leading_zero[1], 4);
    EXPECT_EQ(trailing_zero[2], 7);
    EXPECT_EQ(width[3], 1);
}

TEST(SimdBitTest, RotateAlgorithmsSupportScalarAndMixedVectorCounts) {
    const uint4 values = load_vec<uint4>(std::array<unsigned, 4>{{1u, 2u, 4u, 8u}});
    const int4 shifts = load_vec<int4>(std::array<int, 4>{{0, 1, 2, 3}});
    const uint4 right_values = load_vec<uint4>(std::array<unsigned, 4>{{16u, 8u, 32u, 64u}});
    const int4 right_shifts = load_vec<int4>(std::array<int, 4>{{2, 1, 3, 4}});

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

TEST(SimdBitTest, ShiftAlgorithmsDefineNegativeAndOutOfRangeCounts) {
    const int4 values =
        load_vec<int4>(std::array<int, 4>{{-8, -1, 1, 3}});
    const int4 shifts =
        load_vec<int4>(std::array<int, 4>{{1, 2, -1, 40}});

    const auto vector_left = std::simd::shl(values, shifts);
    const auto vector_right = std::simd::shr(values, shifts);
    const auto scalar_left = std::simd::shl(values, -1);
    const auto scalar_right = std::simd::shr(values, -1);

    EXPECT_EQ(vector_left[0], -16);
    EXPECT_EQ(vector_left[1], -4);
    EXPECT_EQ(vector_left[2], 0);
    EXPECT_EQ(vector_left[3], 0);

    EXPECT_EQ(vector_right[0], -4);
    EXPECT_EQ(vector_right[1], -1);
    EXPECT_EQ(vector_right[2], 2);
    EXPECT_EQ(vector_right[3], 0);

    EXPECT_EQ(scalar_left[0], -4);
    EXPECT_EQ(scalar_left[2], 0);
    EXPECT_EQ(scalar_right[0], -16);
    EXPECT_EQ(scalar_right[2], 2);
}

TEST(SimdBitTest, BitPermutationAlgorithmsApplyScalarAndVectorPatterns) {
    const uint4 values = load_vec<uint4>(
        std::array<unsigned, 4>{{0xcu, 0x2u, 0x5u, 0x1u}});
    const int4 repeat_lengths =
        load_vec<int4>(std::array<int, 4>{{4, 4, 4, 4}});
    const uint4 masks = load_vec<uint4>(
        std::array<unsigned, 4>{{0xau, 0x5u, 0xcu, 0x3u}});

    const auto repeated_scalar = std::simd::bit_repeat(values, 4);
    const auto repeated_vector =
        std::simd::bit_repeat(values, repeat_lengths);
    const auto compressed_scalar =
        std::simd::bit_compress(values, 0xau);
    const auto compressed_vector =
        std::simd::bit_compress(values, masks);
    const auto expanded_scalar =
        std::simd::bit_expand(values, 0xau);
    const auto expanded_vector =
        std::simd::bit_expand(values, masks);

    EXPECT_EQ(repeated_scalar[0], 0xccccccccu);
    EXPECT_EQ(repeated_scalar[1], 0x22222222u);
    EXPECT_EQ(repeated_scalar[2], 0x55555555u);
    EXPECT_EQ(repeated_scalar[3], 0x11111111u);
    for (std::simd::simd_size_type i = 0; i < uint4::size; ++i) {
        EXPECT_EQ(repeated_vector[i], repeated_scalar[i]);
    }

    EXPECT_EQ(compressed_scalar[0], 2u);
    EXPECT_EQ(compressed_vector[0], 2u);
    EXPECT_EQ(compressed_vector[1], 0u);
    EXPECT_EQ(compressed_vector[2], 1u);
    EXPECT_EQ(compressed_vector[3], 1u);

    EXPECT_EQ(expanded_scalar[0], 0u);
    EXPECT_EQ(expanded_scalar[1], 0x8u);
    EXPECT_EQ(expanded_scalar[2], 0x2u);
    EXPECT_EQ(expanded_scalar[3], 0x2u);
    EXPECT_EQ(expanded_vector[0], 0u);
    EXPECT_EQ(expanded_vector[1], 0x4u);
    EXPECT_EQ(expanded_vector[2], 0x4u);
    EXPECT_EQ(expanded_vector[3], 0x1u);
}

} // namespace

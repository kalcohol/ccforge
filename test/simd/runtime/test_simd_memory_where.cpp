#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace simd_test;

int4 make_int4(int v0, int v1, int v2, int v3) {
    const std::array<int, 4> data{{v0, v1, v2, v3}};
    return std::simd::partial_load<int4>(data.data(), 4);
}

TEST(SimdMemoryWhereTest, WhereExpressionAssignsAndCopiesOnlySelectedLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 other = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::where(selected, values) = other;
    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 30);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) = 7;
    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], 4);

    std::array<int, 4> from{{100, 200, 300, 400}};
    std::simd::where(selected, values).copy_from(from.data());
    EXPECT_EQ(values[0], 100);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 300);
    EXPECT_EQ(values[3], 4);

    std::array<int, 4> to{{-1, -1, -1, -1}};
    std::simd::where(selected, values).copy_to(to.data());
    EXPECT_EQ(to[0], 100);
    EXPECT_EQ(to[1], -1);
    EXPECT_EQ(to[2], 300);
    EXPECT_EQ(to[3], -1);

    const int4& const_values = values;
    std::array<int, 4> to_const{{-1, -1, -1, -1}};
    std::simd::where(selected, const_values).copy_to(to_const.data());
    EXPECT_EQ(to_const[0], 100);
    EXPECT_EQ(to_const[1], -1);
    EXPECT_EQ(to_const[2], 300);
    EXPECT_EQ(to_const[3], -1);
}

TEST(SimdMemoryWhereTest, WhereExpressionCompoundAssignmentsModifyOnlySelectedLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 add = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::where(selected, values) += add;
    EXPECT_EQ(values[0], 11);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 33);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) *= 2;
    EXPECT_EQ(values[0], 22);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 66);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) -= make_int4(2, 2, 2, 2);
    EXPECT_EQ(values[0], 20);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 64);
    EXPECT_EQ(values[3], 4);

    {
        int4 div_values = make_int4(100, 10, 60, 40);
        const int4 divisors = make_int4(5, 2, 3, 2);

        std::simd::where(selected, div_values) /= divisors;
        EXPECT_EQ(div_values[0], 20);
        EXPECT_EQ(div_values[1], 10);
        EXPECT_EQ(div_values[2], 20);
        EXPECT_EQ(div_values[3], 40);
    }

    {
        int4 mod_values = make_int4(17, 18, 19, 20);
        const int4 moduli = make_int4(5, 7, 6, 4);

        std::simd::where(selected, mod_values) %= moduli;
        EXPECT_EQ(mod_values[0], 2);
        EXPECT_EQ(mod_values[1], 18);
        EXPECT_EQ(mod_values[2], 1);
        EXPECT_EQ(mod_values[3], 20);
    }

    {
        int4 and_values = make_int4(14, 1, 11, 5);
        const int4 and_rhs = make_int4(10, 0, 12, 0);

        std::simd::where(selected, and_values) &= and_rhs;
        EXPECT_EQ(and_values[0], 10);
        EXPECT_EQ(and_values[1], 1);
        EXPECT_EQ(and_values[2], 8);
        EXPECT_EQ(and_values[3], 5);
    }

    {
        int4 or_values = make_int4(8, 0, 2, 0);
        const int4 or_rhs = make_int4(3, 0, 8, 0);

        std::simd::where(selected, or_values) |= or_rhs;
        EXPECT_EQ(or_values[0], 11);
        EXPECT_EQ(or_values[1], 0);
        EXPECT_EQ(or_values[2], 10);
        EXPECT_EQ(or_values[3], 0);
    }

    {
        int4 xor_values = make_int4(10, 0, 12, 0);
        const int4 xor_rhs = make_int4(15, 0, 3, 0);

        std::simd::where(selected, xor_values) ^= xor_rhs;
        EXPECT_EQ(xor_values[0], 5);
        EXPECT_EQ(xor_values[1], 0);
        EXPECT_EQ(xor_values[2], 15);
        EXPECT_EQ(xor_values[3], 0);
    }

    {
        int4 shift_values = make_int4(1, 2, 3, 4);
        std::simd::where(selected, shift_values) <<= 2;
        EXPECT_EQ(shift_values[0], 4);
        EXPECT_EQ(shift_values[1], 2);
        EXPECT_EQ(shift_values[2], 12);
        EXPECT_EQ(shift_values[3], 4);

        std::simd::where(selected, shift_values) >>= 1;
        EXPECT_EQ(shift_values[0], 2);
        EXPECT_EQ(shift_values[2], 6);
    }

    {
        int4 mod_scalar = make_int4(17, 18, 19, 20);
        std::simd::where(selected, mod_scalar) %= 4;
        EXPECT_EQ(mod_scalar[0], 1);
        EXPECT_EQ(mod_scalar[2], 3);
    }

    {
        int4 and_scalar = make_int4(14, 1, 11, 5);
        std::simd::where(selected, and_scalar) &= 6;
        EXPECT_EQ(and_scalar[0], 6);
        EXPECT_EQ(and_scalar[2], 2);
    }

    {
        int4 or_scalar = make_int4(8, 0, 2, 0);
        std::simd::where(selected, or_scalar) |= 1;
        EXPECT_EQ(or_scalar[0], 9);
        EXPECT_EQ(or_scalar[2], 3);
    }

    {
        int4 xor_scalar = make_int4(10, 0, 12, 0);
        std::simd::where(selected, xor_scalar) ^= 3;
        EXPECT_EQ(xor_scalar[0], 9);
        EXPECT_EQ(xor_scalar[2], 15);
    }

    {
        int4 vec_lshift = make_int4(1, 2, 4, 8);
        const int4 lshifts = make_int4(3, 2, 1, 0);
        std::simd::where(selected, vec_lshift) <<= lshifts;
        EXPECT_EQ(vec_lshift[0], 8);
        EXPECT_EQ(vec_lshift[1], 2);
        EXPECT_EQ(vec_lshift[2], 8);
        EXPECT_EQ(vec_lshift[3], 8);
    }

    {
        int4 vec_rshift = make_int4(16, 8, 32, 64);
        const int4 rshifts = make_int4(2, 1, 3, 4);
        std::simd::where(selected, vec_rshift) >>= rshifts;
        EXPECT_EQ(vec_rshift[0], 4);
        EXPECT_EQ(vec_rshift[1], 8);
        EXPECT_EQ(vec_rshift[2], 4);
        EXPECT_EQ(vec_rshift[3], 64);
    }
}

TEST(SimdMemoryWhereTest, WhereBoolOverloadAssignsAllOrNoLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 other = make_int4(9, 8, 7, 6);

    std::simd::where(true, values) = other;
    EXPECT_EQ(values[0], 9);
    EXPECT_EQ(values[1], 8);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], 6);

    values = make_int4(1, 2, 3, 4);
    std::simd::where(false, values) = other;
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
    EXPECT_EQ(values[3], 4);
}

} // namespace

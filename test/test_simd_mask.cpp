#include <simd>

#include <bitset>

#include <gtest/gtest.h>

namespace {

using std::simd::mask;
using int4 = std::simd::vec<int, 4>;
using longlong4 = std::simd::vec<long long, 4>;
using byte_mask4 = std::simd::mask<signed char, 4>;
using wide_byte_mask128 = std::simd::mask<signed char, 128>;

TEST(SimdMaskTest, LogicalOperatorsApplyPerLane) {
    mask<int, 4> left(0b0101u);
    mask<int, 4> right(0b0011u);

    const auto and_value = left && right;
    const auto or_value = left || right;
    const auto not_value = !left;

    EXPECT_TRUE(and_value[0]);
    EXPECT_FALSE(and_value[1]);
    EXPECT_FALSE(and_value[2]);
    EXPECT_FALSE(and_value[3]);

    EXPECT_TRUE(or_value[0]);
    EXPECT_TRUE(or_value[1]);
    EXPECT_TRUE(or_value[2]);
    EXPECT_FALSE(or_value[3]);

    EXPECT_FALSE(not_value[0]);
    EXPECT_TRUE(not_value[1]);
    EXPECT_FALSE(not_value[2]);
    EXPECT_TRUE(not_value[3]);
}

TEST(SimdMaskTest, BitwiseOperatorsApplyPerLane) {
    mask<int, 4> left(0b0101u);
    mask<int, 4> right(0b0011u);

    auto and_value = left & right;
    auto or_value = left | right;
    auto xor_value = left ^ right;

    left &= right;
    right |= mask<int, 4>(0b0100u);

    EXPECT_TRUE(and_value[0]);
    EXPECT_FALSE(and_value[1]);
    EXPECT_FALSE(and_value[2]);
    EXPECT_FALSE(and_value[3]);

    EXPECT_TRUE(or_value[0]);
    EXPECT_TRUE(or_value[1]);
    EXPECT_TRUE(or_value[2]);
    EXPECT_FALSE(or_value[3]);

    EXPECT_FALSE(xor_value[0]);
    EXPECT_TRUE(xor_value[1]);
    EXPECT_TRUE(xor_value[2]);
    EXPECT_FALSE(xor_value[3]);

    EXPECT_TRUE(left[0]);
    EXPECT_FALSE(left[1]);
    EXPECT_FALSE(left[2]);
    EXPECT_FALSE(left[3]);

    EXPECT_TRUE(right[0]);
    EXPECT_TRUE(right[1]);
    EXPECT_TRUE(right[2]);
    EXPECT_FALSE(right[3]);
}

TEST(SimdMaskTest, SelectChoosesMaskLanesFromBranchMasks) {
    const mask<int, 4> cond(0b0101u);
    const mask<int, 4> when_true(0b0011u);
    const mask<int, 4> when_false(0b1100u);

    const auto blended = std::simd::select(cond, when_true, when_false);

    // Lane-wise: lane0/2 from when_true; lane1/3 from when_false.
    EXPECT_TRUE(blended[0]);
    EXPECT_FALSE(blended[1]);
    EXPECT_FALSE(blended[2]);
    EXPECT_TRUE(blended[3]);
}

#if defined(FORGE_SIMD_HAS_WHERE_MASK)

TEST(SimdMaskTest, WhereExpressionAssignsOnlySelectedMaskLanes) {
    const mask<int, 4> cond(0b0101u);
    mask<int, 4> target(0b0000u);
    const mask<int, 4> other(0b1111u);

    std::simd::where(cond, target) = other;

    EXPECT_TRUE(target[0]);
    EXPECT_FALSE(target[1]);
    EXPECT_TRUE(target[2]);
    EXPECT_FALSE(target[3]);
}

#endif

TEST(SimdMaskTest, ReductionsTrackPartialMasks) {
    mask<int, 4> values(0b0110u);
    mask<int, 4> all_true(0b1111u);
    mask<int, 4> all_false(0u);

    EXPECT_TRUE(std::simd::all_of(all_true));
    EXPECT_FALSE(std::simd::all_of(values));

    EXPECT_TRUE(std::simd::any_of(values));
    EXPECT_FALSE(std::simd::any_of(all_false));

    EXPECT_FALSE(std::simd::none_of(values));
    EXPECT_TRUE(std::simd::none_of(all_false));

    EXPECT_EQ(std::simd::reduce_count(values), 2u);
    EXPECT_EQ(std::simd::reduce_min_index(values), 1u);
    EXPECT_EQ(std::simd::reduce_max_index(values), 2u);
}

TEST(SimdMaskTest, BoolOverloadsMatchMaskSemantics) {
    EXPECT_TRUE(std::simd::all_of(true));
    EXPECT_TRUE(std::simd::any_of(true));
    EXPECT_FALSE(std::simd::none_of(true));
    EXPECT_EQ(std::simd::reduce_count(true), 1u);

    EXPECT_FALSE(std::simd::all_of(false));
    EXPECT_FALSE(std::simd::any_of(false));
    EXPECT_TRUE(std::simd::none_of(false));
    EXPECT_EQ(std::simd::reduce_count(false), 0u);
}

TEST(SimdMaskTest, ConstructorsAndConversionsPreserveBitPatterns) {
    std::bitset<4> bits;
    bits.set(0);
    bits.set(2);

    mask<int, 4> from_bits(bits);
    mask<int, 4> implicit_from_bits = bits;
    mask<int, 4> from_unsigned(0b1010u);
    byte_mask4 source(0b0101u);
    mask<int, 4> from_other(source);
    const auto roundtrip = from_bits.to_bitset();
    const auto encoded = from_bits.to_ullong();
    const int4 implicit_as_int = from_bits;
    const auto as_int = static_cast<int4>(from_bits);
    const auto as_longlong = static_cast<longlong4>(from_bits);

    EXPECT_TRUE(from_bits[0]);
    EXPECT_TRUE(implicit_from_bits[0]);
    EXPECT_FALSE(from_bits[1]);
    EXPECT_TRUE(from_bits[2]);
    EXPECT_FALSE(from_bits[3]);

    EXPECT_FALSE(from_unsigned[0]);
    EXPECT_TRUE(from_unsigned[1]);
    EXPECT_FALSE(from_unsigned[2]);
    EXPECT_TRUE(from_unsigned[3]);

    EXPECT_TRUE(from_other[0]);
    EXPECT_FALSE(from_other[1]);
    EXPECT_TRUE(from_other[2]);
    EXPECT_FALSE(from_other[3]);

    EXPECT_TRUE(roundtrip[0]);
    EXPECT_FALSE(roundtrip[1]);
    EXPECT_TRUE(roundtrip[2]);
    EXPECT_FALSE(roundtrip[3]);
    EXPECT_EQ(encoded, 0b0101ull);

    EXPECT_EQ(implicit_as_int[0], 1);
    EXPECT_EQ(implicit_as_int[1], 0);
    EXPECT_EQ(implicit_as_int[2], 1);
    EXPECT_EQ(implicit_as_int[3], 0);

    EXPECT_EQ(as_int[0], 1);
    EXPECT_EQ(as_int[1], 0);
    EXPECT_EQ(as_int[2], 1);
    EXPECT_EQ(as_int[3], 0);

    EXPECT_EQ(as_longlong[0], 1);
    EXPECT_EQ(as_longlong[1], 0);
    EXPECT_EQ(as_longlong[2], 1);
    EXPECT_EQ(as_longlong[3], 0);
}

TEST(SimdMaskTest, WideMasksToUllongEncodeOnlyTheLow64Bits) {
    const wide_byte_mask128 low_bits([](auto lane) {
        return decltype(lane)::value == 0 || decltype(lane)::value == 63;
    });
    const wide_byte_mask128 high_bits([](auto lane) {
        return decltype(lane)::value == 100;
    });
    const wide_byte_mask128 mixed_bits([](auto lane) {
        return decltype(lane)::value == 0 || decltype(lane)::value == 63 || decltype(lane)::value == 100;
    });

    constexpr unsigned long long low_mask_bits = (1ull << 0) | (1ull << 63);

    EXPECT_EQ(low_bits.to_ullong(), low_mask_bits);
    EXPECT_EQ(high_bits.to_ullong(), 0ull);
    EXPECT_EQ(mixed_bits.to_ullong(), low_mask_bits);
}

TEST(SimdMaskTest, UnaryOperatorsProduceExpectedIntegerLanes) {
    mask<int, 4> values(0b0101u);

    const auto positive = +values;
    const auto negative = -values;
    const auto inverted = ~values;

    EXPECT_EQ(positive[0], 1);
    EXPECT_EQ(positive[1], 0);
    EXPECT_EQ(positive[2], 1);
    EXPECT_EQ(positive[3], 0);

    EXPECT_EQ(negative[0], -1);
    EXPECT_EQ(negative[1], 0);
    EXPECT_EQ(negative[2], -1);
    EXPECT_EQ(negative[3], 0);

    EXPECT_EQ(inverted[0], -2);
    EXPECT_EQ(inverted[1], -1);
    EXPECT_EQ(inverted[2], -2);
    EXPECT_EQ(inverted[3], -1);
}

#if defined(FORGE_SIMD_HAS_LONG_DOUBLE_MASK)
TEST(SimdMaskTest, LongDoubleMaskUnaryInstantiates) {
    std::simd::mask<long double, 2> values(true);
    auto positive = +values;
    auto negative = -values;
    auto inverted = ~values;

    EXPECT_EQ(static_cast<int>(positive[0]), 1);
    EXPECT_EQ(static_cast<int>(negative[0]), -1);
    EXPECT_EQ(static_cast<int>(inverted[0]), -2);
}
#endif

#if defined(FORGE_SIMD_HAS_NATIVE_MASK_UNARY)
TEST(SimdMaskTest, NativeFloatMaskUnaryInstantiates) {
    std::simd::mask<float, 4> float_mask(0b0101u);
    std::simd::mask<double, 2> double_mask(0b01u);
    std::simd::mask<unsigned int, 4> uint_mask(0b0011u);
    auto positive = +float_mask;
    auto negative = -double_mask;
    auto inverted = ~uint_mask;

    EXPECT_EQ(static_cast<int>(positive[0]), 1);
    EXPECT_EQ(static_cast<int>(negative[0]), -1);
    EXPECT_EQ(static_cast<int>(inverted[0]), -2);
}
#endif

TEST(SimdMaskTest, ComparisonsApplyPerLane) {
    mask<int, 4> left(0b1100u);
    mask<int, 4> right(0b1010u);

    const auto equal_value = left == right;
    const auto not_equal_value = left != right;
    const auto less_value = left < right;
    const auto less_equal_value = left <= right;
    const auto greater_value = left > right;
    const auto greater_equal_value = left >= right;

    EXPECT_TRUE(equal_value[0]);
    EXPECT_FALSE(equal_value[1]);
    EXPECT_FALSE(equal_value[2]);
    EXPECT_TRUE(equal_value[3]);

    EXPECT_FALSE(not_equal_value[0]);
    EXPECT_TRUE(not_equal_value[1]);
    EXPECT_TRUE(not_equal_value[2]);
    EXPECT_FALSE(not_equal_value[3]);

    EXPECT_FALSE(less_value[0]);
    EXPECT_TRUE(less_value[1]);
    EXPECT_FALSE(less_value[2]);
    EXPECT_FALSE(less_value[3]);

    EXPECT_TRUE(less_equal_value[0]);
    EXPECT_TRUE(less_equal_value[1]);
    EXPECT_FALSE(less_equal_value[2]);
    EXPECT_TRUE(less_equal_value[3]);

    EXPECT_FALSE(greater_value[0]);
    EXPECT_FALSE(greater_value[1]);
    EXPECT_TRUE(greater_value[2]);
    EXPECT_FALSE(greater_value[3]);

    EXPECT_TRUE(greater_equal_value[0]);
    EXPECT_FALSE(greater_equal_value[1]);
    EXPECT_TRUE(greater_equal_value[2]);
    EXPECT_TRUE(greater_equal_value[3]);
}

} // namespace

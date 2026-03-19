#include <simd>

#include <gtest/gtest.h>

#include <array>

namespace {

using int4 = std::simd::vec<int, 4>;
using float4 = std::simd::vec<float, 4>;
using longlong4 = std::simd::vec<long long, 4>;
using mask4 = std::simd::mask<int, 4>;
using longlong_mask4 = std::simd::mask<long long, 4>;

template<class V, class T, size_t N>
V load_vec(const std::array<T, N>& values) {
    return std::simd::partial_load<V>(values.data(), static_cast<std::simd::simd_size_type>(N));
}

int4 make_int4(int v0, int v1, int v2, int v3) {
    const std::array<int, 4> data{{v0, v1, v2, v3}};
    return load_vec<int4>(data);
}

TEST(SimdOperatorsTest, UnaryOperatorsApplyPerLane) {
    int4 values = make_int4(1, -2, 3, -4);

    const auto positive = +values;
    const auto negative = -values;

    EXPECT_EQ(positive[0], 1);
    EXPECT_EQ(positive[1], -2);
    EXPECT_EQ(negative[0], -1);
    EXPECT_EQ(negative[1], 2);
    EXPECT_EQ(negative[2], -3);
    EXPECT_EQ(negative[3], 4);
}

TEST(SimdOperatorsTest, CompoundAssignmentsUpdateAllLanes) {
    int4 values = make_int4(10, 20, 30, 40);

    values += make_int4(1, 2, 3, 4);
    values -= make_int4(1, 1, 1, 1);
    values *= make_int4(2, 2, 2, 2);
    values /= make_int4(2, 3, 4, 5);

    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 14);
    EXPECT_EQ(values[2], 16);
    EXPECT_EQ(values[3], 17);
}

TEST(SimdOperatorsTest, ConversionConstructorCastsEachLane) {
    int4 source = make_int4(1, 2, 3, 4);
    float4 converted(source, std::simd::flag_convert);

    EXPECT_FLOAT_EQ(converted[0], 1.0f);
    EXPECT_FLOAT_EQ(converted[1], 2.0f);
    EXPECT_FLOAT_EQ(converted[2], 3.0f);
    EXPECT_FLOAT_EQ(converted[3], 4.0f);
}

TEST(SimdOperatorsTest, SimdCastAndStaticSimdCastConvertEachLane) {
    const int4 source = make_int4(1, 2, 3, 4);

    const auto widened = std::simd::simd_cast<longlong4>(source);
    EXPECT_EQ(widened[0], 1);
    EXPECT_EQ(widened[3], 4);

    const auto narrowed = std::simd::static_simd_cast<int4>(widened);
    EXPECT_EQ(narrowed[0], 1);
    EXPECT_EQ(narrowed[3], 4);
}

#if defined(FORGE_SIMD_HAS_MASK_CAST)

TEST(SimdOperatorsTest, MaskSimdCastAndStaticSimdCastPreserveSelectedLanes) {
    const mask4 source(0b0101u);

    const auto widened = std::simd::simd_cast<longlong_mask4>(source);
    EXPECT_TRUE(widened[0]);
    EXPECT_FALSE(widened[1]);
    EXPECT_TRUE(widened[2]);
    EXPECT_FALSE(widened[3]);

    const auto roundtrip = std::simd::static_simd_cast<mask4>(widened);
    EXPECT_TRUE(roundtrip[0]);
    EXPECT_FALSE(roundtrip[1]);
    EXPECT_TRUE(roundtrip[2]);
    EXPECT_FALSE(roundtrip[3]);
}

#endif

TEST(SimdOperatorsTest, ComparisonOperatorsRemainLaneWise) {
    int4 left = make_int4(1, 2, 3, 4);
    int4 right = make_int4(1, 3, 2, 4);

    const auto equal_mask = left == right;
    const auto not_equal_mask = left != right;
    const auto less_mask = left < right;
    const auto less_equal_mask = left <= right;
    const auto greater_mask = left > right;
    const auto greater_equal_mask = left >= right;

    EXPECT_TRUE(equal_mask[0]);
    EXPECT_FALSE(equal_mask[1]);
    EXPECT_TRUE(not_equal_mask[1]);
    EXPECT_TRUE(less_mask[1]);
    EXPECT_TRUE(less_equal_mask[0]);
    EXPECT_FALSE(greater_mask[1]);
    EXPECT_TRUE(greater_mask[2]);
    EXPECT_TRUE(greater_equal_mask[3]);
}

} // namespace

#include <simd>

#include <gtest/gtest.h>

namespace {

using int4 = std::simd::vec<int, 4>;
using float4 = std::simd::vec<float, 4>;

TEST(SimdOperatorsTest, UnaryOperatorsApplyPerLane) {
    int4 values{1, -2, 3, -4};

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
    int4 values{10, 20, 30, 40};

    values += int4{1, 2, 3, 4};
    values -= int4{1, 1, 1, 1};
    values *= int4{2, 2, 2, 2};
    values /= int4{2, 3, 4, 5};

    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 14);
    EXPECT_EQ(values[2], 16);
    EXPECT_EQ(values[3], 17);
}

TEST(SimdOperatorsTest, ConversionConstructorCastsEachLane) {
    int4 source{1, 2, 3, 4};
    float4 converted(source, std::simd::flag_convert);

    EXPECT_FLOAT_EQ(converted[0], 1.0f);
    EXPECT_FLOAT_EQ(converted[1], 2.0f);
    EXPECT_FLOAT_EQ(converted[2], 3.0f);
    EXPECT_FLOAT_EQ(converted[3], 4.0f);
}

TEST(SimdOperatorsTest, ComparisonOperatorsRemainLaneWise) {
    int4 left{1, 2, 3, 4};
    int4 right{1, 3, 2, 4};

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

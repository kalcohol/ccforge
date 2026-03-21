#include "simd_test_common.hpp"

#include <gtest/gtest.h>

namespace {

using namespace simd_test;

static_assert(std::is_same<typename std::simd::vec<int, 4>::mask_type, std::simd::mask<int, 4>>::value,
    "vec<int, 4> should expose mask<int, 4> as mask_type");

TEST(SimdRuntimeTest, FixedSizeAliasConstructsExpectedLaneCount) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);

    EXPECT_EQ(decltype(values)::size, 4u);
    EXPECT_EQ(lane(values, 0), 1);
    EXPECT_EQ(lane(values, 1), 2);
    EXPECT_EQ(lane(values, 2), 3);
    EXPECT_EQ(lane(values, 3), 4);
}

TEST(SimdRuntimeTest, DefaultWidthAliasBroadcastsAcrossAllLanes) {
    std::simd::vec<float> values(2.5f);

    EXPECT_GE(decltype(values)::size, 1u);

    for (std::simd::simd_size_type i = 0; i < decltype(values)::size; ++i) {
        EXPECT_FLOAT_EQ(lane(values, i), 2.5f);
    }
}

TEST(SimdRuntimeTest, ValuePreservingVectorConstructionWidensLanes) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 ints = load_vec<int4>(data);
    const longlong4 widened(ints);

    EXPECT_EQ(widened[0], 1);
    EXPECT_EQ(widened[3], 4);
}

TEST(SimdRuntimeTest, ExplicitScalarSourceBroadcastsAcrossAllLanes) {
    const int4 values(explicit_to_int{});

    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(values[3], 7);
}

TEST(SimdRuntimeTest, BoolScalarBroadcastConvertsToLaneType) {
    const int4 values(true);

    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[3], 1);
}

TEST(SimdRuntimeTest, ContiguousRangeConstructionLoadsLanesInOrder) {
    const std::array<int, 4> data{{5, 6, 7, 8}};
    const std::span<const int, 4> values_view(data);
    const int4 values(values_view);

    EXPECT_EQ(values[0], 5);
    EXPECT_EQ(values[3], 8);
}

TEST(SimdRuntimeTest, GeneratorConstructorUsesLaneIndices) {
    int4 values([](auto index) {
        return static_cast<int>(decltype(index)::value * 3 + 1);
    });

    EXPECT_EQ(lane(values, 0), 1);
    EXPECT_EQ(lane(values, 1), 4);
    EXPECT_EQ(lane(values, 2), 7);
    EXPECT_EQ(lane(values, 3), 10);
}

TEST(SimdRuntimeTest, IotaGeneratesIncrementingSequenceFromStart) {
    const auto values = std::simd::iota<int4>(3);

    EXPECT_EQ(values[0], 3);
    EXPECT_EQ(values[1], 4);
    EXPECT_EQ(values[2], 5);
    EXPECT_EQ(values[3], 6);
}

TEST(SimdRuntimeTest, IotaDefaultsToZeroForIntegers) {
    const auto values = std::simd::iota<int4>();

    EXPECT_EQ(values[0], 0);
    EXPECT_EQ(values[1], 1);
    EXPECT_EQ(values[2], 2);
    EXPECT_EQ(values[3], 3);
}

TEST(SimdRuntimeTest, IotaSupportsFloatingPointStarts) {
    const auto values = std::simd::iota<std::simd::vec<float, 4>>(1.5f);

    EXPECT_FLOAT_EQ(values[0], 1.5f);
    EXPECT_FLOAT_EQ(values[1], 2.5f);
    EXPECT_FLOAT_EQ(values[2], 3.5f);
    EXPECT_FLOAT_EQ(values[3], 4.5f);
}

TEST(SimdRuntimeTest, ScalarSelectBuildsLaneVectorFromMask) {
    const mask4 selected(0b0101u);
    const auto values = std::simd::select(selected, 7, -3);

    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(values[1], -3);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], -3);
}

TEST(SimdRuntimeTest, BeginEndAndDefaultSentinelTraverseAllLanes) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    int4 values = load_vec<int4>(data);
    auto begin = values.begin();
    auto end = values.end();
    int sum = 0;

    EXPECT_EQ(end - begin, 4);
    EXPECT_EQ(begin - end, -4);

    auto post_inc = begin++;
    EXPECT_EQ(*post_inc, 1);
    EXPECT_EQ(*begin, 2);

    auto third = values.begin() + 2;
    auto post_dec = third--;
    EXPECT_EQ(*post_dec, 3);
    EXPECT_EQ(*third, 2);

    while (begin != end) {
        sum += *begin;
        ++begin;
    }

    const int4& const_values = values;
    auto cbegin = const_values.cbegin();
    auto cend = const_values.cend();
    typename int4::const_iterator converted = values.begin();

    EXPECT_EQ(sum, 9);
    EXPECT_EQ(cend - cbegin, 4);
    EXPECT_EQ(*cbegin, 1);
    EXPECT_EQ(cbegin[2], 3);
    EXPECT_EQ(*(cbegin + 2), 3);
    EXPECT_EQ(*converted, 1);
}

TEST(SimdRuntimeTest, ArithmeticProducesPerLaneResults) {
    const std::array<int, 4> left_data{{1, 2, 3, 4}};
    const std::array<int, 4> right_data{{4, 3, 2, 1}};
    const int4 left = load_vec<int4>(left_data);
    const int4 right = load_vec<int4>(right_data);

    const auto sum = left + right;
    const auto diff = left - right;
    const auto prod = left * right;

    EXPECT_EQ(lane(sum, 0), 5);
    EXPECT_EQ(lane(sum, 3), 5);
    EXPECT_EQ(lane(diff, 0), -3);
    EXPECT_EQ(lane(diff, 3), 3);
    EXPECT_EQ(lane(prod, 0), 4);
    EXPECT_EQ(lane(prod, 3), 4);
}

TEST(SimdRuntimeTest, ComparisonReturnsMaskAlias) {
    const std::array<int, 4> left_data{{1, 3, 5, 7}};
    const std::array<int, 4> right_data{{1, 4, 2, 7}};
    const int4 left = load_vec<int4>(left_data);
    const int4 right = load_vec<int4>(right_data);

    const auto equal_mask = left == right;
    const auto greater_mask = left > right;

    EXPECT_TRUE(lane(equal_mask, 0));
    EXPECT_FALSE(lane(equal_mask, 1));
    EXPECT_TRUE(lane(equal_mask, 3));
    EXPECT_FALSE(lane(greater_mask, 0));
    EXPECT_FALSE(lane(greater_mask, 1));
    EXPECT_TRUE(lane(greater_mask, 2));
    EXPECT_FALSE(lane(greater_mask, 3));
}

TEST(SimdRuntimeTest, MaskedRangeConstructorLoadsOnlySelectedLanes) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const std::span<const int, 4> view(data);
    const mask4 selected(0b0101u);

    const int4 values(view, selected);

    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 0);
    EXPECT_EQ(values[2], 30);
    EXPECT_EQ(values[3], 0);
}

TEST(SimdRuntimeTest, ConstexprWrapperLikeConstructionBroadcastsValue) {
    const int4 values(std::integral_constant<int, 42>{});

    EXPECT_EQ(values[0], 42);
    EXPECT_EQ(values[1], 42);
    EXPECT_EQ(values[2], 42);
    EXPECT_EQ(values[3], 42);
}

} // namespace

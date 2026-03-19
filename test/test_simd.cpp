#include <simd>

#include <gtest/gtest.h>

#include <limits>
#include <tuple>
#include <type_traits>

namespace {

using std::simd::mask;
using std::simd::vec;

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

static_assert(std::is_same<typename vec<int, 4>::mask_type, mask<int, 4>>::value,
    "vec<int, 4> should expose mask<int, 4> as mask_type");

} // namespace

TEST(SimdRuntimeTest, FixedSizeAliasConstructsExpectedLaneCount) {
    vec<int, 4> values{1, 2, 3, 4};

    EXPECT_EQ(decltype(values)::size, 4u);
    EXPECT_EQ(lane(values, 0), 1);
    EXPECT_EQ(lane(values, 1), 2);
    EXPECT_EQ(lane(values, 2), 3);
    EXPECT_EQ(lane(values, 3), 4);
}

TEST(SimdRuntimeTest, DefaultWidthAliasBroadcastsAcrossAllLanes) {
    vec<float> values(2.5f);

    EXPECT_GE(decltype(values)::size, 1u);

    for (std::simd::simd_size_type i = 0; i < decltype(values)::size; ++i) {
        EXPECT_FLOAT_EQ(lane(values, i), 2.5f);
    }
}

#if defined(FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)

TEST(SimdRuntimeExpansionTest, ChunkByTypeSplitsIntoFixedChunks) {
    std::simd::resize_t<8, vec<int, 4>> values{1, 2, 3, 4, 5, 6, 7, 8};
    auto parts = std::simd::chunk<vec<int, 4>>(values);

    EXPECT_EQ(parts[0][0], 1);
    EXPECT_EQ(parts[0][3], 4);
    EXPECT_EQ(parts[1][0], 5);
    EXPECT_EQ(parts[1][3], 8);
    EXPECT_EQ(std::simd::cat(parts[0], parts[1])[6], 7);
}

TEST(SimdRuntimeExpansionTest, ChunkByWidthUsesLaneCountSemantics) {
    std::simd::resize_t<8, vec<int, 4>> values{1, 2, 3, 4, 5, 6, 7, 8};
    auto parts = std::simd::chunk<2>(values);

    EXPECT_EQ(parts[0][0], 1);
    EXPECT_EQ(parts[0][1], 2);
    EXPECT_EQ(parts[1][0], 3);
    EXPECT_EQ(parts[1][1], 4);
    EXPECT_EQ(parts[3][0], 7);
    EXPECT_EQ(parts[3][1], 8);
    EXPECT_EQ(std::simd::cat(parts[0], parts[1], parts[2], parts[3])[6], 7);
}

TEST(SimdRuntimeExpansionTest, ChunkReturnsTailForUnevenWidths) {
    vec<int, 4> values{1, 2, 3, 4};
    auto pieces = std::simd::chunk<3>(values);
    const auto& full = std::get<0>(pieces);
    const auto& tail = std::get<1>(pieces);

    EXPECT_EQ(full[0][0], 1);
    EXPECT_EQ(full[0][1], 2);
    EXPECT_EQ(full[0][2], 3);
    EXPECT_EQ(tail[0], 4);
}

TEST(SimdRuntimeExpansionTest, CatConcatenatesFixedPieces) {
    vec<int, 4> lo{1, 2, 3, 4};
    vec<int, 4> hi{5, 6, 7, 8};
    const auto joined = std::simd::cat(lo, hi);

    EXPECT_EQ(joined[0], 1);
    EXPECT_EQ(joined[7], 8);
}

TEST(SimdRuntimeExpansionTest, PermuteReordersLanes) {
    vec<int, 4> values{1, 2, 3, 4};
    const auto reversed = std::simd::permute(values, [](auto index) {
        return std::simd::simd_size_type(3 - decltype(index)::value);
    });
    const auto reversed_with_size = std::simd::permute(values, [](auto index, auto size) {
        return std::simd::simd_size_type(decltype(size)::value - 1 - decltype(index)::value);
    });
    const auto indexed = values[vec<int, 4>{2, 0, 3, 1}];

    EXPECT_EQ(reversed[0], 4);
    EXPECT_EQ(reversed[3], 1);
    EXPECT_EQ(reversed_with_size[0], 4);
    EXPECT_EQ(reversed_with_size[3], 1);
    EXPECT_EQ(indexed[0], 3);
    EXPECT_EQ(indexed[1], 1);
    EXPECT_EQ(indexed[2], 4);
    EXPECT_EQ(indexed[3], 2);
}

#else

TEST(SimdDraftRuntimeTest, ChunkCatPermuteCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS once chunk/cat/permute land.";
}

#endif

TEST(SimdRuntimeTest, GeneratorConstructorUsesLaneIndices) {
    vec<int, 4> values([](auto index) {
        return static_cast<int>(decltype(index)::value * 3 + 1);
    });

    EXPECT_EQ(lane(values, 0), 1);
    EXPECT_EQ(lane(values, 1), 4);
    EXPECT_EQ(lane(values, 2), 7);
    EXPECT_EQ(lane(values, 3), 10);
}

TEST(SimdRuntimeTest, BeginEndAndDefaultSentinelTraverseAllLanes) {
    vec<int, 4> values{1, 2, 3, 4};
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

    const vec<int, 4>& const_values = values;
    auto cbegin = const_values.cbegin();
    auto cend = const_values.cend();
    typename vec<int, 4>::const_iterator converted = values.begin();

    EXPECT_EQ(sum, 9);
    EXPECT_EQ(cend - cbegin, 4);
    EXPECT_EQ(*cbegin, 1);
    EXPECT_EQ(cbegin[2], 3);
    EXPECT_EQ(*(cbegin + 2), 3);
    EXPECT_EQ(*converted, 1);
}

TEST(SimdRuntimeTest, ArithmeticProducesPerLaneResults) {
    vec<int, 4> left{1, 2, 3, 4};
    vec<int, 4> right{4, 3, 2, 1};

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
    vec<int, 4> left{1, 3, 5, 7};
    vec<int, 4> right{1, 4, 2, 7};

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

TEST(SimdRuntimeTest, ReduceFamilyProducesExpectedValues) {
    vec<int, 4> values{1, 4, 2, 3};
    mask<int, 4> selected{true, false, true, false};
    mask<int, 4> none_selected(false);

    EXPECT_EQ(std::simd::reduce(values), 10);
    EXPECT_EQ(std::simd::reduce(values, selected), 3);
    EXPECT_EQ(std::simd::reduce_min(values), 1);
    EXPECT_EQ(std::simd::reduce_min(values, selected), 1);
    EXPECT_EQ(std::simd::reduce_min(values, none_selected), std::numeric_limits<int>::max());
    EXPECT_EQ(std::simd::reduce_max(values), 4);
    EXPECT_EQ(std::simd::reduce_max(values, selected), 2);
    EXPECT_EQ(std::simd::reduce_max(values, none_selected), std::numeric_limits<int>::lowest());
}

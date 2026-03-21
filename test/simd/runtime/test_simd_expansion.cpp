#include "simd_test_common.hpp"

#include <gtest/gtest.h>

namespace {

using namespace simd_test;

#if defined(FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)

TEST(SimdRuntimeExpansionTest, ChunkByTypeSplitsIntoFixedChunks) {
    const std::array<int, 8> data{{1, 2, 3, 4, 5, 6, 7, 8}};
    const int8 values = load_vec<int8>(data);
    auto parts = std::simd::chunk<int4>(values);

    EXPECT_EQ(parts[0][0], 1);
    EXPECT_EQ(parts[0][3], 4);
    EXPECT_EQ(parts[1][0], 5);
    EXPECT_EQ(parts[1][3], 8);
    EXPECT_EQ(std::simd::cat(parts[0], parts[1])[6], 7);
}

TEST(SimdRuntimeExpansionTest, ChunkByWidthUsesLaneCountSemantics) {
    const std::array<int, 8> data{{1, 2, 3, 4, 5, 6, 7, 8}};
    const int8 values = load_vec<int8>(data);
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
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);
    auto pieces = std::simd::chunk<3>(values);
    const auto& full = std::get<0>(pieces);
    const auto& tail = std::get<1>(pieces);

    EXPECT_EQ(full[0][0], 1);
    EXPECT_EQ(full[0][1], 2);
    EXPECT_EQ(full[0][2], 3);
    EXPECT_EQ(tail[0], 4);
}

TEST(SimdRuntimeExpansionTest, CatConcatenatesFixedPieces) {
    const std::array<int, 4> lo_data{{1, 2, 3, 4}};
    const std::array<int, 4> hi_data{{5, 6, 7, 8}};
    const int4 lo = load_vec<int4>(lo_data);
    const int4 hi = load_vec<int4>(hi_data);
    const auto joined = std::simd::cat(lo, hi);

    EXPECT_EQ(joined[0], 1);
    EXPECT_EQ(joined[7], 8);
}

TEST(SimdRuntimeExpansionTest, PermuteReordersLanes) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);
    const auto reversed = std::simd::permute(values, [](auto index) {
        return std::simd::simd_size_type(3 - decltype(index)::value);
    });
    const auto reversed_with_size = std::simd::permute(values, [](auto index, auto size) {
        return std::simd::simd_size_type(decltype(size)::value - 1 - decltype(index)::value);
    });
    const std::array<int, 4> indices_data{{2, 0, 3, 1}};
    const int4 indices = load_vec<int4>(indices_data);
    const auto indexed = values[indices];

    EXPECT_EQ(reversed[0], 4);
    EXPECT_EQ(reversed[3], 1);
    EXPECT_EQ(reversed_with_size[0], 4);
    EXPECT_EQ(reversed_with_size[3], 1);
    EXPECT_EQ(indexed[0], 3);
    EXPECT_EQ(indexed[1], 1);
    EXPECT_EQ(indexed[2], 4);
    EXPECT_EQ(indexed[3], 2);
}

TEST(SimdRuntimeExpansionTest, PermuteSupportsZeroAndUninitSentinels) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);
    const auto permuted = std::simd::permute(values, [](auto index) {
        if constexpr (decltype(index)::value == 0) {
            return std::simd::simd_size_type(1);
        } else if constexpr (decltype(index)::value == 1) {
            return std::simd::zero_element;
        } else if constexpr (decltype(index)::value == 2) {
            return std::simd::simd_size_type(3);
        } else {
            return std::simd::uninit_element;
        }
    });

    EXPECT_EQ(permuted[0], 2);
    EXPECT_EQ(permuted[1], 0);
    EXPECT_EQ(permuted[2], 4);
    EXPECT_EQ(permuted[3], 0);
}

#else

TEST(SimdDraftRuntimeTest, ChunkCatPermuteCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS once chunk/cat/permute land.";
}

#endif

TEST(SimdRuntimeTest, SplitByChunkTypeMatchesLaneOrder) {
    const std::array<int, 8> data{{1, 2, 3, 4, 5, 6, 7, 8}};
    const int8 values = load_vec<int8>(data);
    const auto parts = std::simd::split<int4>(values);

    EXPECT_EQ(parts[0][0], 1);
    EXPECT_EQ(parts[0][3], 4);
    EXPECT_EQ(parts[1][0], 5);
    EXPECT_EQ(parts[1][3], 8);
}

TEST(SimdRuntimeTest, SplitBySizesSupportsUnevenPieces) {
    const std::array<int, 8> data{{1, 2, 3, 4, 5, 6, 7, 8}};
    const int8 values = load_vec<int8>(data);
    const auto pieces = std::simd::split<2, 3, 3>(values);
    const auto& first = std::get<0>(pieces);
    const auto& second = std::get<1>(pieces);
    const auto& third = std::get<2>(pieces);

    EXPECT_EQ(first[0], 1);
    EXPECT_EQ(first[1], 2);
    EXPECT_EQ(second[0], 3);
    EXPECT_EQ(second[2], 5);
    EXPECT_EQ(third[0], 6);
    EXPECT_EQ(third[2], 8);
}

TEST(SimdRuntimeTest, CompressAndExpandRearrangeLanesUsingMask) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);

    const auto packed = std::simd::compress(values, selected);
    EXPECT_EQ(packed[0], 10);
    EXPECT_EQ(packed[1], 30);
    EXPECT_EQ(packed[2], 0);
    EXPECT_EQ(packed[3], 0);

    const auto expanded = std::simd::expand(packed, selected);
    EXPECT_EQ(expanded[0], 10);
    EXPECT_EQ(expanded[1], 0);
    EXPECT_EQ(expanded[2], 30);
    EXPECT_EQ(expanded[3], 0);
}

TEST(SimdRuntimeTest, CompressAndExpandHandleAllTrueMasks) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b1111u);

    const auto packed = std::simd::compress(values, selected);
    EXPECT_EQ(packed[0], 10);
    EXPECT_EQ(packed[3], 40);

    const auto expanded = std::simd::expand(packed, selected);
    EXPECT_EQ(expanded[0], 10);
    EXPECT_EQ(expanded[3], 40);
}

TEST(SimdRuntimeTest, CompressAndExpandHandleAllFalseMasks) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0u);

    const auto packed = std::simd::compress(values, selected);
    EXPECT_EQ(packed[0], 0);
    EXPECT_EQ(packed[3], 0);

    const auto expanded = std::simd::expand(packed, selected);
    EXPECT_EQ(expanded[0], 0);
    EXPECT_EQ(expanded[3], 0);
}

TEST(SimdRuntimeTest, ExpandAfterCompressRestoresSelectedLanes) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0110u);

    const auto roundtrip = std::simd::expand(std::simd::compress(values, selected), selected);
    EXPECT_EQ(roundtrip[0], 0);
    EXPECT_EQ(roundtrip[1], 20);
    EXPECT_EQ(roundtrip[2], 30);
    EXPECT_EQ(roundtrip[3], 0);
}

TEST(SimdRuntimeTest, CompressFillAndExpandOriginalPreserveInactiveLanes) {
    const std::array<int, 4> data{{10, 20, 30, 40}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);

    const auto packed = std::simd::compress(values, selected, -1);
    EXPECT_EQ(packed[0], 10);
    EXPECT_EQ(packed[1], 30);
    EXPECT_EQ(packed[2], -1);
    EXPECT_EQ(packed[3], -1);

    const auto expanded = std::simd::expand(packed, selected, values);
    EXPECT_EQ(expanded[0], 10);
    EXPECT_EQ(expanded[1], 20);
    EXPECT_EQ(expanded[2], 30);
    EXPECT_EQ(expanded[3], 40);
}

} // namespace

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace {

using namespace simd_test;

int4 make_int4(int v0, int v1, int v2, int v3) {
    const std::array<int, 4> data{{v0, v1, v2, v3}};
    return std::simd::partial_load<int4>(data.data(), 4);
}

TEST(SimdMemoryGatherScatterTest, UncheckedGatherAndScatterRoundTrip) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(6, 0, 3, 7);

    const auto gathered = std::simd::unchecked_gather_from<int4>(input.data(), indices);
    EXPECT_EQ(gathered[0], 16);
    EXPECT_EQ(gathered[1], 10);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 17);

    std::simd::unchecked_scatter_to(gathered, output.data(), indices);
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output[3], 13);
    EXPECT_EQ(output[6], 16);
    EXPECT_EQ(output[7], 17);
    EXPECT_EQ(output[1], -1);
}

TEST(SimdMemoryGatherScatterTest, PartialGatherZeroFillsOffsetsOutsideCount) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    const int4 indices = make_int4(6, 0, 3, 7);

    const auto gathered = std::simd::partial_gather_from<int4>(input.data(), 6, indices);
    EXPECT_EQ(gathered[0], 0);
    EXPECT_EQ(gathered[1], 10);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);
}

TEST(SimdMemoryGatherScatterTest, PartialGatherHonorsMaskAndCount) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    const int4 indices = make_int4(6, 0, 3, 7);
    const mask4 selected(0b0101u);

    const auto gathered = std::simd::partial_gather_from<int4>(input.data(), 6, selected, indices);
    EXPECT_EQ(gathered[0], 0);
    EXPECT_EQ(gathered[1], 0);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);
}

TEST(SimdMemoryGatherScatterTest, PartialGatherAndScatterSupportContiguousRanges) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const std::span<const int, 8> input_view(input);
    std::span<int, 8> output_view(output);

    const auto gathered = std::simd::partial_gather_from(input_view, indices);
    static_assert(std::is_same<std::remove_const_t<decltype(gathered)>, int4>::value,
        "range gather should default to vec<range_value_t<R>, Indices::size()>");
    std::simd::partial_scatter_to(gathered, output_view, indices);

    EXPECT_EQ(gathered[0], 11);
    EXPECT_EQ(gathered[1], 14);
    EXPECT_EQ(gathered[2], 15);
    EXPECT_EQ(gathered[3], 17);
    EXPECT_EQ(output[1], 11);
    EXPECT_EQ(output[4], 14);
    EXPECT_EQ(output[5], 15);
    EXPECT_EQ(output[7], 17);
}

TEST(SimdMemoryGatherScatterTest, PartialScatterSkipsOffsetsOutsideCount) {
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const int4 values = make_int4(10, 20, 30, 40);

    std::simd::partial_scatter_to(values, output.data(), 6, indices);

    EXPECT_EQ(output[1], 10);
    EXPECT_EQ(output[4], 20);
    EXPECT_EQ(output[5], 30);
    EXPECT_EQ(output[7], -1);
}

TEST(SimdMemoryGatherScatterTest, PartialScatterHonorsMaskAndCount) {
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const int4 values = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::partial_scatter_to(values, output.data(), 6, selected, indices);

    EXPECT_EQ(output[1], 10);
    EXPECT_EQ(output[4], -1);
    EXPECT_EQ(output[5], 30);
    EXPECT_EQ(output[7], -1);
}

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)

TEST(SimdMemoryGatherScatterExtensionTest, UncheckedMaskedGatherAndScatterUseStandardOrder) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(6, 0, 3, 7);
    const mask4 selected(0b0101u);

    const auto gathered = std::simd::unchecked_gather_from<int4>(input.data(), selected, indices);
    EXPECT_EQ(gathered[0], 16);
    EXPECT_EQ(gathered[1], 0);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);

    std::simd::unchecked_scatter_to(gathered, output.data(), selected, indices);
    EXPECT_EQ(output[0], -1);
    EXPECT_EQ(output[3], 13);
    EXPECT_EQ(output[6], 16);
    EXPECT_EQ(output[7], -1);
}

#endif

} // namespace

#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's gather/scatter index bounds."
#endif

static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::int64_t) == 8 && sizeof(std::uint64_t) == 8);

#ifdef FORGE_SIMD_REQUIRE_ILP32_INDEX_BOUNDS
static_assert(sizeof(void*) == 4 && sizeof(std::ptrdiff_t) == 4);
static_assert(sizeof(std::simd::simd_size_type) == 4);
#endif

namespace {

using vector4 = std::simd::vec<int, 4>;
using unsigned_indices4 = std::simd::vec<std::uint64_t, 4>;
using signed_indices4 = std::simd::vec<std::int64_t, 4>;
using size_type = std::simd::simd_size_type;
constexpr std::uint64_t high_unsigned = std::uint64_t{1} << 32;
constexpr std::int64_t high_signed = std::int64_t{1} << 32;

template<class V>
void expect_lanes(const V& value, const std::array<typename V::value_type, 4>& expected) {
    for (size_type lane = 0; lane < 4; ++lane) {
        EXPECT_EQ(value[lane], expected[static_cast<std::size_t>(lane)]);
    }
}

vector4 scatter_values() {
    return vector4([](auto lane) { return 10 * (1 + static_cast<int>(decltype(lane)::value)); });
}

TEST(SimdGatherScatterIndexBounds, Auxiliary32BitExtentDoesNotNarrowWideIndices) {
    using std::simd::detail::is_gather_scatter_index_in_range;
    const std::array<std::uint64_t, 6> unsigned_values{
        0, 3, 4, high_unsigned + 1, high_unsigned + 2, std::numeric_limits<std::uint64_t>::max()};
    const std::array<bool, 6> unsigned_expected{true, true, false, false, false, false};
    for (std::size_t i = 0; i < unsigned_values.size(); ++i) {
        EXPECT_EQ(is_gather_scatter_index_in_range(unsigned_values[i], std::int32_t{4}),
            unsigned_expected[i]);
    }
    const std::array<std::int64_t, 7> signed_values{
        -1, 0, 3, 4, high_signed + 1, -high_signed + 2, std::numeric_limits<std::int64_t>::min()};
    const std::array<bool, 7> signed_expected{false, true, true, false, false, false, false};
    for (std::size_t i = 0; i < signed_values.size(); ++i) {
        EXPECT_EQ(is_gather_scatter_index_in_range(signed_values[i], std::int32_t{4}), signed_expected[i]);
    }
    EXPECT_FALSE(is_gather_scatter_index_in_range(std::uint64_t{0}, std::int32_t{0}));
    EXPECT_FALSE(is_gather_scatter_index_in_range(std::uint64_t{0}, std::int32_t{-1}));
    EXPECT_TRUE(is_gather_scatter_index_in_range(std::uint8_t{255}, std::int64_t{256}));
}

TEST(SimdGatherScatterIndexBounds, UnsignedWideOffsetsDoNotAliasPointerElements) {
    const std::array<int, 4> input{11, 22, 33, 44};
    const std::array<std::uint64_t, 4> lanes{0, high_unsigned + 1, high_unsigned + 2, 3};
    const unsigned_indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const unsigned_indices4::mask_type selected(0b1100u);
    const auto gathered = std::simd::partial_gather_from<vector4>(input.data(), 4, indices);
    const auto masked = std::simd::partial_gather_from<vector4>(input.data(), 4, selected, indices);
    expect_lanes(gathered, {11, 0, 0, 44});
    expect_lanes(masked, {0, 0, 0, 44});

    const vector4 values = scatter_values();
    std::array<int, 4> output{-1, -1, -1, -1};
    std::simd::partial_scatter_to(values, output.data(), 4, indices);
    EXPECT_EQ(output, (std::array<int, 4>{10, -1, -1, 40}));
    output.fill(-1);
    std::simd::partial_scatter_to(values, output.data(), 4, selected, indices);
    EXPECT_EQ(output, (std::array<int, 4>{-1, -1, -1, 40}));
}

TEST(SimdGatherScatterIndexBounds, SignedWideOffsetsDoNotAliasContiguousIteratorElements) {
    const std::array<int, 4> input{11, 22, 33, 44};
    const std::array<std::int64_t, 4> lanes{0, high_signed + 1, -high_signed + 2, 3};
    const signed_indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const signed_indices4::mask_type selected(0b1100u);
    const auto first = std::counted_iterator<const int*>(input.data(), 4);
    const auto gathered = std::simd::partial_gather_from<vector4>(first, 4, indices);
    const auto masked = std::simd::partial_gather_from<vector4>(first, 4, selected, indices);
    expect_lanes(gathered, {11, 0, 0, 44});
    expect_lanes(masked, {0, 0, 0, 44});

    const vector4 values = scatter_values();
    std::array<int, 4> output{-1, -1, -1, -1};
    std::simd::partial_scatter_to(values, std::counted_iterator<int*>(output.data(), 4), 4, indices);
    EXPECT_EQ(output, (std::array<int, 4>{10, -1, -1, 40}));
    output.fill(-1);
    std::simd::partial_scatter_to(values,
        std::counted_iterator<int*>(output.data(), 4), 4, selected, indices);
    EXPECT_EQ(output, (std::array<int, 4>{-1, -1, -1, 40}));
}

TEST(SimdGatherScatterIndexBounds, RangeForwardingPreservesBoundsAndConversionFlags) {
    using float4 = std::simd::vec<float, 4>;
    const std::array<double, 4> input{1.5, 2.5, 3.5, 4.5};
    const std::array<std::uint64_t, 4> lanes{0, high_unsigned + 1, high_unsigned + 2, 3};
    const unsigned_indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const unsigned_indices4::mask_type selected(0b1100u);
    const auto gathered = std::simd::partial_gather_from<float4>(input, indices, std::simd::flag_convert);
    const auto masked = std::simd::partial_gather_from<float4>(
        input, selected, indices, std::simd::flag_convert);
    expect_lanes(gathered, {1.5f, 0.0f, 0.0f, 4.5f});
    expect_lanes(masked, {0.0f, 0.0f, 0.0f, 4.5f});

    const float4 values([](auto lane) { return 1.5f + static_cast<float>(decltype(lane)::value); });
    std::array<double, 4> output{-1.0, -1.0, -1.0, -1.0};
    std::simd::partial_scatter_to(values, output, indices, std::simd::flag_convert);
    EXPECT_EQ(output, (std::array<double, 4>{1.5, -1.0, -1.0, 4.5}));
    output.fill(-1.0);
    std::simd::partial_scatter_to(values, output, selected, indices, std::simd::flag_convert);
    EXPECT_EQ(output, (std::array<double, 4>{-1.0, -1.0, -1.0, 4.5}));
}

TEST(SimdGatherScatterIndexBounds, CharacterIndicesAndEmptyExtentsRemainSupported) {
    const std::array<int, 4> input{11, 22, 33, 44};
    using character_indices4 = std::simd::vec<char8_t, 4>;
    const std::array<char8_t, 4> lanes{0, 3, 4, 5};
    const character_indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const auto gathered = std::simd::partial_gather_from<vector4>(input.data(), 4, indices);
    expect_lanes(gathered, {11, 44, 0, 0});
    const vector4 values = scatter_values();
    std::array<int, 4> output{-1, -1, -1, -1};
    std::simd::partial_scatter_to(values, output.data(), 4, indices);
    EXPECT_EQ(output, (std::array<int, 4>{10, -1, -1, 20}));

    const character_indices4::mask_type selected(true);
    const auto empty = std::simd::partial_gather_from<vector4>(input.data(), 0, indices);
    const auto masked_empty = std::simd::partial_gather_from<vector4>(input.data(), 0, selected, indices);
    expect_lanes(empty, {0, 0, 0, 0});
    expect_lanes(masked_empty, {0, 0, 0, 0});
    output.fill(-1);
    std::simd::partial_scatter_to(values, output.data(), 0, indices);
    std::simd::partial_scatter_to(values, output.data(), 0, selected, indices);
    EXPECT_EQ(output, (std::array<int, 4>{-1, -1, -1, -1}));
}

TEST(SimdGatherScatterIndexBounds, UncheckedControlsUseOnlyValidSelectedIndices) {
    const std::array<int, 4> input{11, 22, 33, 44};
    const std::array<std::uint64_t, 4> lanes{0, high_unsigned + 1, high_unsigned + 2, 3};
    const unsigned_indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const unsigned_indices4::mask_type selected(0b1001u);
    const auto gathered = std::simd::unchecked_gather_from<vector4>(input, selected, indices);
    expect_lanes(gathered, {11, 0, 0, 44});
    std::array<int, 4> output{-1, -1, -1, -1};
    std::simd::unchecked_scatter_to(scatter_values(), output, selected, indices);
    EXPECT_EQ(output, (std::array<int, 4>{10, -1, -1, 40}));

    const unsigned_indices4 valid([](auto lane) { return std::uint64_t{3} - decltype(lane)::value; });
    const auto all = std::simd::unchecked_gather_from<vector4>(input, valid);
    expect_lanes(all, {44, 33, 22, 11});
    std::simd::unchecked_scatter_to(all, output, valid);
    EXPECT_EQ(output, input);
}

} // namespace

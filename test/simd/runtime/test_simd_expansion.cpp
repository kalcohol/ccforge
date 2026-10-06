#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using namespace simd_test;

template<class V, class Selector>
concept can_compress = requires(const V& values, const Selector& selector) {
    { std::simd::compress(values, selector) } -> std::same_as<V>;
};

template<class V, class Selector, class Fill>
concept can_compress_with_fill = requires(const V& values, const Selector& selector, const Fill& fill) {
    { std::simd::compress(values, selector, fill) } -> std::same_as<V>;
};

template<class V, class Selector>
concept can_expand = requires(const V& values, const Selector& selector) {
    { std::simd::expand(values, selector) } -> std::same_as<V>;
};

template<class V, class Selector, class Original>
concept can_expand_with_original = requires(const V& values, const Selector& selector, const Original& original) {
    { std::simd::expand(values, selector, original) } -> std::same_as<V>;
};

struct implicit_compress_fill {
    float fill;
    int* conversions;

    operator float() const& noexcept {
        ++*conversions;
        return fill;
    }
};

struct explicit_compress_fill {
    explicit operator float() const;
};

struct throwing_compress_fill {
    operator float() const {
        throw std::runtime_error("compress fill conversion");
    }
};

struct implicit_mask_selector {
    mask4 lanes;
    int* conversions;

    operator mask4() const& noexcept {
        ++*conversions;
        return lanes;
    }
};

struct explicit_mask_selector {
    explicit operator mask4() const;
};

struct constexpr_mask_selector {
    constexpr operator mask4() const noexcept { return mask4(std::bitset<4>(0b1010u)); }
};

using float_mask4 = typename float4::mask_type;
using double_mask4 = typename double4::mask_type;
using mask2 = std::simd::mask<int, 2>;

static_assert(can_compress_with_fill<float4, float_mask4, int>);
static_assert(can_compress_with_fill<float4, float_mask4, double>);
static_assert(!std::is_convertible_v<double, float4>);
static_assert(can_compress_with_fill<float4, float_mask4, implicit_compress_fill>);
static_assert(can_compress_with_fill<float4, float_mask4, throwing_compress_fill>);
static_assert(!can_compress_with_fill<float4, float_mask4, explicit_compress_fill>);
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
static_assert(noexcept(std::simd::compress(std::declval<const float4&>(),
    std::declval<const float_mask4&>(), std::declval<const implicit_compress_fill&>())));
#endif
static_assert(!noexcept(std::simd::compress(std::declval<const float4&>(),
    std::declval<const float_mask4&>(), std::declval<const throwing_compress_fill&>())));

static_assert(can_compress<int4, std::bitset<4>>);
static_assert(can_compress<int4, implicit_mask_selector>);
static_assert(can_expand<int4, std::bitset<4>>);
static_assert(can_expand_with_original<int4, implicit_mask_selector, int4>);
static_assert(!can_compress<int4, explicit_mask_selector>);
static_assert(!can_expand<int4, explicit_mask_selector>);
static_assert(can_compress<mask4, std::bitset<4>>);
static_assert(can_compress_with_fill<mask4, std::bitset<4>, bool>);
static_assert(can_compress<mask4, implicit_mask_selector>);
static_assert(can_compress_with_fill<mask4, implicit_mask_selector, bool>);
static_assert(can_expand<mask4, std::bitset<4>>);
static_assert(can_expand_with_original<mask4, std::bitset<4>, mask4>);
static_assert(can_expand<mask4, implicit_mask_selector>);
static_assert(can_expand_with_original<mask4, implicit_mask_selector, mask4>);
static_assert(!can_compress<mask4, explicit_mask_selector>);
static_assert(!can_compress_with_fill<mask4, explicit_mask_selector, bool>);
static_assert(!can_expand<mask4, explicit_mask_selector>);
static_assert(!can_expand_with_original<mask4, explicit_mask_selector, mask4>);
static_assert(!can_compress<mask4, bool>);
static_assert(!can_compress<mask4, unsigned>);
static_assert(!can_compress<mask4, byte_mask4>);
static_assert(!can_compress<mask4, mask2>);
static_assert(!can_compress<mask4, std::bitset<3>>);
static_assert(!can_expand<mask4, bool>);
static_assert(!can_expand<mask4, unsigned>);
static_assert(!can_expand<mask4, byte_mask4>);
static_assert(!can_expand<mask4, mask2>);
static_assert(!can_expand<mask4, std::bitset<3>>);

// Only the selector is non-deduced; original still fixes the same type and width.
static_assert(can_expand_with_original<float4, float_mask4, float4>);
static_assert(std::is_convertible_v<const int4&, double4>);
static_assert(!can_expand_with_original<double4, double_mask4, int4>);
static_assert(!can_expand_with_original<int4, mask4, int2>);
static_assert(!can_expand_with_original<mask4, std::bitset<4>, mask2>);
static_assert(!can_expand_with_original<mask4, std::bitset<4>, byte_mask4>);
static_assert(std::is_convertible_v<const implicit_mask_selector&, mask4>);
static_assert(!can_expand_with_original<mask4, std::bitset<4>, implicit_mask_selector>);
static_assert(!can_expand_with_original<mask4, std::bitset<4>, std::bitset<4>>);
static_assert(!can_compress<implicit_mask_selector, mask4>);
static_assert(!can_expand<implicit_mask_selector, mask4>);

constexpr bool nondeduced_compress_expand_parameters() {
    const float4 values([](auto lane) { return static_cast<float>(decltype(lane)::value + 1); });
    const float_mask4 selected{std::bitset<4>(0b0101u)};
    const auto zero_filled = std::simd::compress(values, selected, 0);
    const auto converted_fill = std::simd::compress(values, selected, 0.5);
    const mask4 bits{std::bitset<4>(0b1001u)};
    const auto packed = std::simd::compress(bits, constexpr_mask_selector{}, true);
    const auto expanded = std::simd::expand(packed, constexpr_mask_selector{}, mask4(false));
    return zero_filled[0] == 1.0f && zero_filled[1] == 3.0f &&
        zero_filled[2] == 0.0f && zero_filled[3] == 0.0f &&
        converted_fill[2] == 0.5f && converted_fill[3] == 0.5f &&
        !packed[0] && packed[1] && packed[2] && packed[3] &&
        !expanded[0] && !expanded[1] && !expanded[2] && expanded[3];
}
static_assert(nondeduced_compress_expand_parameters());

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
    using int5 = std::simd::vec<int, 5>;
    const std::array<int, 5> data{{1, 2, 3, 4, 5}};
    const int5 values = load_vec<int5>(data);
    auto [first, second, tail] = std::simd::chunk<int2>(values);

    EXPECT_EQ(first[0], 1);
    EXPECT_EQ(first[1], 2);
    EXPECT_EQ(second[0], 3);
    EXPECT_EQ(second[1], 4);
    EXPECT_EQ(tail[0], 5);
}

TEST(SimdRuntimeExpansionTest, MaskChunkReturnsFlatTailTuple) {
    using mask5 = std::simd::mask<int, 5>;
    using mask2 = std::simd::mask<int, 2>;
    const mask5 values([](auto lane) {
        return decltype(lane)::value % 2 == 0;
    });
    auto [first, second, tail] = std::simd::chunk<mask2>(values);

    EXPECT_TRUE(first[0]);
    EXPECT_FALSE(first[1]);
    EXPECT_TRUE(second[0]);
    EXPECT_FALSE(second[1]);
    EXPECT_TRUE(tail[0]);
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
}

#else

TEST(SimdDraftRuntimeTest, ChunkCatPermuteCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS once chunk/cat/permute land.";
}

#endif

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

TEST(SimdRuntimeExpansionTest, CompressFillUsesOrdinaryImplicitConversionOnce) {
    const float4 values = load_vec<float4>(std::array<float, 4>{{10.0f, 20.0f, 30.0f, 40.0f}});
    const float_mask4 selected{std::bitset<4>(0b0101u)};
    const auto integer_fill = std::simd::compress(values, selected, 0);
    const auto double_fill = std::simd::compress(values, selected, 0.1);
    int conversions = 0;
    const implicit_compress_fill fill{-2.5f, &conversions};
    const auto custom_fill = std::simd::compress(values, selected, fill);
    EXPECT_EQ(conversions, 1);
    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_FLOAT_EQ(integer_fill[i], i < 2 ? values[i * 2] : 0.0f);
        EXPECT_FLOAT_EQ(double_fill[i], i < 2 ? values[i * 2] : static_cast<float>(0.1));
        EXPECT_FLOAT_EQ(custom_fill[i], i < 2 ? values[i * 2] : -2.5f);
    }
    conversions = 0;
    const auto unchanged = std::simd::compress(values, float_mask4(true), fill);
    EXPECT_EQ(conversions, 1);
    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_FLOAT_EQ(unchanged[i], values[i]);
    }
    EXPECT_THROW(std::simd::compress(values, selected, throwing_compress_fill{}), std::runtime_error);
}

TEST(SimdRuntimeExpansionTest, MaskCompressAndExpandAcceptBitsetSelectors) {
    const mask4 values{std::bitset<4>(0b1001u)};
    const std::bitset<4> selected(0b1010u);
    const mask4 original{std::bitset<4>(0b0101u)};
    const auto unspecified_tail = std::simd::compress(values, selected);
    EXPECT_FALSE(unspecified_tail[0]);
    EXPECT_TRUE(unspecified_tail[1]);
    const auto packed = std::simd::compress(values, selected, true);
    const std::array<bool, 4> packed_expected{{false, true, true, true}};
    const auto expanded = std::simd::expand(packed, selected, original);
    const std::array<bool, 4> expanded_expected{{true, false, true, true}};
    for (std::simd::simd_size_type i = 0; i < mask4::size; ++i) {
        EXPECT_EQ(packed[i], packed_expected[i]);
        EXPECT_EQ(expanded[i], expanded_expected[i]);
    }
    const auto default_original = std::simd::expand(packed, std::bitset<4>(0b1010u));
    EXPECT_FALSE(default_original[1]);
    EXPECT_TRUE(default_original[3]);
}

TEST(SimdRuntimeExpansionTest, MaskSelectorProxyConvertsOncePerCall) {
    int conversions = 0;
    const implicit_mask_selector selected{mask4(std::bitset<4>(0b1010u)), &conversions};
    const mask4 values{std::bitset<4>(0b1001u)};
    const auto unspecified_tail = std::simd::compress(values, selected);
    EXPECT_EQ(conversions, 1);
    EXPECT_FALSE(unspecified_tail[0]);
    EXPECT_TRUE(unspecified_tail[1]);
    conversions = 0;
    const auto packed = std::simd::compress(values, selected, false);
    EXPECT_EQ(conversions, 1);
    const std::array<bool, 4> packed_expected{{false, true, false, false}};
    conversions = 0;
    const auto expanded = std::simd::expand(packed, selected, mask4(true));
    EXPECT_EQ(conversions, 1);
    const std::array<bool, 4> expanded_expected{{true, false, true, true}};
    for (std::simd::simd_size_type i = 0; i < mask4::size; ++i) {
        EXPECT_EQ(packed[i], packed_expected[i]);
        EXPECT_EQ(expanded[i], expanded_expected[i]);
    }
    conversions = 0;
    const auto default_original = std::simd::expand(packed,
        implicit_mask_selector{selected.lanes, &conversions});
    EXPECT_EQ(conversions, 1);
    EXPECT_FALSE(default_original[1]);
    EXPECT_TRUE(default_original[3]);
}

} // namespace

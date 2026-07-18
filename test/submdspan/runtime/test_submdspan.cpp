// Runtime tests for std::submdspan
// Tests correctness of submdspan with layout_left, layout_right, layout_stride,
// padded layouts, and current-draft range slice spellings.

#include <mdspan>
#include <array>
#include <tuple>
#include <type_traits>
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fill a contiguous buffer with sequential integers starting at 0
template<std::size_t N>
auto make_data() {
    std::array<int, N> d{};
    for (int i = 0; i < (int)N; ++i) d[i] = i;
    return d;
}

// ---------------------------------------------------------------------------
// layout_right (default, row-major) tests
// ---------------------------------------------------------------------------

TEST(SubmdspanLayoutRight, IndexPlusFullExtent) {
    auto data = make_data<12>();
    // 3x4 row-major: m[i,j] = i*4 + j
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    auto row1 = std::submdspan(m, 1, std::full_extent);
    EXPECT_EQ(row1.rank(), 1u);
    EXPECT_EQ(row1.extent(0), 4u);
    EXPECT_EQ(row1[0], 4);
    EXPECT_EQ(row1[1], 5);
    EXPECT_EQ(row1[3], 7);
}

TEST(SubmdspanLayoutRight, FullExtentPreservesLayout) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    auto full = std::submdspan(m, std::full_extent, std::full_extent);
    EXPECT_EQ(full.rank(), 2u);
    EXPECT_EQ(full.extent(0), 3u);
    EXPECT_EQ(full.extent(1), 4u);
    // layout_right should be preserved
    EXPECT_TRUE((std::is_same_v<decltype(full)::layout_type, std::layout_right>));
    EXPECT_EQ((full[2, 3]), 11);
}

TEST(SubmdspanLayoutRight, PairRange) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // rows [0,2) = first 2 rows
    auto sub = std::submdspan(m, std::pair{0,2}, std::full_extent);
    EXPECT_EQ(sub.rank(), 2u);
    EXPECT_EQ(sub.extent(0), 2u);
    EXPECT_EQ(sub.extent(1), 4u);
    EXPECT_EQ((sub[0, 0]), 0);
    EXPECT_EQ((sub[1, 0]), 4);
    EXPECT_EQ((sub[1, 3]), 7);
}

TEST(SubmdspanLayoutRight, TupleRange) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // Use std::tuple as pair-like slice
    auto sub = std::submdspan(m, std::tuple{1, 3}, std::full_extent);
    EXPECT_EQ(sub.rank(), 2u);
    EXPECT_EQ(sub.extent(0), 2u);  // rows 1 and 2
    EXPECT_EQ((sub[0, 0]), 4);  // m[1,0]
    EXPECT_EQ((sub[1, 0]), 8);  // m[2,0]
}

TEST(SubmdspanLayoutRight, StridedSlice) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // current extent_slice: extent is the output count, selecting 0 and 2
    auto sub = std::submdspan(m, 0, std::extent_slice{0, 2, 2});
    EXPECT_EQ(sub.rank(), 1u);
    EXPECT_EQ(sub.extent(0), 2u);
    EXPECT_EQ(sub[0], 0);  // m[0,0]
    EXPECT_EQ(sub[1], 2);  // m[0,2]
}

TEST(SubmdspanLayoutRight, RangeSlice) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // range_slice{first=0, last=4, stride=2}: selects 0 and 2
    auto sub = std::submdspan(m, 0, std::range_slice{0, 4, 2});
    EXPECT_EQ(sub.rank(), 1u);
    EXPECT_EQ(sub.extent(0), 2u);
    EXPECT_EQ(sub[0], 0);
    EXPECT_EQ(sub[1], 2);
}

TEST(SubmdspanLayoutRight, ExtentSliceEmptyExtent) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // extent_slice with extent=0 → 0 elements
    auto sub = std::submdspan(m, 0, std::extent_slice{0, 0, 2});
    EXPECT_EQ(sub.rank(), 1u);
    EXPECT_EQ(sub.extent(0), 0u);
}

TEST(SubmdspanLayoutRight, AllIndexScalar) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // Both dimensions collapsed → rank-0 result
    auto s = std::submdspan(m, 1, 2);
    EXPECT_EQ(s.rank(), 0u);
    EXPECT_EQ(s[], 6);  // m[1,2] = 1*4+2 = 6
}

TEST(SubmdspanLayoutRight, GeneralStridedMapping) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>> m(data.data(), 3, 4);

    // Strided range on first dim + range on second → layout_stride result
    // range_slice{0, 3, 2} on rows: rows 0, 2
    auto sub = std::submdspan(m,
        std::range_slice{0, 3, 2},
        std::full_extent);
    EXPECT_EQ(sub.rank(), 2u);
    EXPECT_EQ(sub.extent(0), 2u);  // rows 0, 2
    EXPECT_EQ(sub.extent(1), 4u);
    EXPECT_EQ((sub[0, 0]), 0);   // m[0,0]
    EXPECT_EQ((sub[1, 0]), 8);   // m[2,0]
    EXPECT_EQ((sub[0, 3]), 3);   // m[0,3]
    EXPECT_EQ((sub[1, 3]), 11);  // m[2,3]
}

// ---------------------------------------------------------------------------
// layout_left (column-major) tests
// ---------------------------------------------------------------------------

TEST(SubmdspanLayoutLeft, FullExtentPreservesLayout) {
    auto data = make_data<12>();
    // 3x4 column-major: ml[i,j] = i + j*3
    std::mdspan<int, std::dextents<int,2>, std::layout_left> ml(data.data(), 3, 4);

    auto full = std::submdspan(ml, std::full_extent, std::full_extent);
    EXPECT_TRUE((std::is_same_v<decltype(full)::layout_type, std::layout_left>));
    EXPECT_EQ(full.rank(), 2u);
    EXPECT_EQ((full[1, 2]), 7);  // 1 + 2*3 = 7
}

TEST(SubmdspanLayoutLeft, FullExtentPlusIndex) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>, std::layout_left> ml(data.data(), 3, 4);

    // Column 0: indices 0, 1, 2
    auto col0 = std::submdspan(ml, std::full_extent, 0);
    EXPECT_EQ(col0.rank(), 1u);
    EXPECT_EQ(col0.extent(0), 3u);
    EXPECT_EQ(col0[0], 0);
    EXPECT_EQ(col0[1], 1);
    EXPECT_EQ(col0[2], 2);
}

TEST(SubmdspanLayoutLeft, IndexPlusFullExtent) {
    auto data = make_data<12>();
    std::mdspan<int, std::dextents<int,2>, std::layout_left> ml(data.data(), 3, 4);

    // Row 0 (col-major): ml[0,j] = j*3
    auto row0 = std::submdspan(ml, 0, std::full_extent);
    EXPECT_EQ(row0.rank(), 1u);
    EXPECT_EQ(row0.extent(0), 4u);
    EXPECT_EQ(row0[0], 0);   // ml[0,0] = 0
    EXPECT_EQ(row0[1], 3);   // ml[0,1] = 3
    EXPECT_EQ(row0[3], 9);   // ml[0,3] = 9
}

// ---------------------------------------------------------------------------
// layout_stride tests
// ---------------------------------------------------------------------------

TEST(SubmdspanLayoutStride, FromStridedMapping) {
    auto data = make_data<20>();
    // Create a strided layout manually: stride[0]=1, stride[1]=5 in 4x4 view
    std::array<int,2> strides{1, 5};
    std::mdspan<int, std::dextents<int,2>, std::layout_stride> ms(
        data.data(), std::layout_stride::mapping{std::dextents<int,2>{4,4}, strides});

    // ms[i,j] = i + j*5 (first 4 rows, first 4 cols)
    EXPECT_EQ((ms[0,0]), 0);
    EXPECT_EQ((ms[1,0]), 1);
    EXPECT_EQ((ms[0,1]), 5);

    auto sub = std::submdspan(ms, std::full_extent, std::full_extent);
    EXPECT_EQ(sub.rank(), 2u);
    EXPECT_EQ((sub[2,3]), 17);  // 2 + 3*5 = 17
}

// ---------------------------------------------------------------------------
// Rank-3 test
// ---------------------------------------------------------------------------

TEST(SubmdspanRank3, SliceMiddleDimension) {
    auto data = make_data<24>();
    // 2x3x4 tensor, layout_right: t[i,j,k] = i*12 + j*4 + k
    std::mdspan<int, std::dextents<int,3>> t(data.data(), 2, 3, 4);

    // Fix j=1: submdspan(t, full_extent, 1, full_extent) → 2x4 slice
    auto s = std::submdspan(t, std::full_extent, 1, std::full_extent);
    EXPECT_EQ(s.rank(), 2u);
    EXPECT_EQ(s.extent(0), 2u);
    EXPECT_EQ(s.extent(1), 4u);
    EXPECT_EQ((s[0,0]), 4);   // t[0,1,0] = 0*12+1*4+0 = 4
    EXPECT_EQ((s[1,3]), 19);  // t[1,1,3] = 1*12+1*4+3 = 19
}

// ---------------------------------------------------------------------------
// padded layout tests
// ---------------------------------------------------------------------------

TEST(SubmdspanPaddedLayouts, LayoutLeftPaddedMapping) {
    using ext_t = std::extents<int, 3, 4>;
    std::layout_left_padded<8>::mapping<ext_t> map(ext_t{}, 8);

    EXPECT_EQ(map.stride(0), 1);
    EXPECT_EQ(map.stride(1), 8);
    EXPECT_EQ((map(2, 3)), 26);
    EXPECT_EQ(map.required_span_size(), 27);
    EXPECT_FALSE(map.is_exhaustive());
}

TEST(SubmdspanPaddedLayouts, LayoutRightPaddedMapping) {
    using ext_t = std::extents<int, 3, 4>;
    std::layout_right_padded<8>::mapping<ext_t> map(ext_t{}, 8);

    EXPECT_EQ(map.stride(0), 8);
    EXPECT_EQ(map.stride(1), 1);
    EXPECT_EQ((map(2, 3)), 19);
    EXPECT_EQ(map.required_span_size(), 20);
    EXPECT_FALSE(map.is_exhaustive());
}

TEST(SubmdspanPaddedLayouts, StaticPaddedStrideControlsAlwaysExhaustive) {
    using left_ext_t = std::extents<int, 8, 2>;
    using right_ext_t = std::extents<int, 2, 8>;
    using left_map_t = std::layout_left_padded<4>::mapping<left_ext_t>;
    using right_map_t = std::layout_right_padded<4>::mapping<right_ext_t>;
    using dynamic_left_map_t = std::layout_left_padded<>::mapping<left_ext_t>;

    static_assert(left_map_t::is_always_exhaustive());
    static_assert(right_map_t::is_always_exhaustive());
    static_assert(!dynamic_left_map_t::is_always_exhaustive());

    left_map_t left{left_ext_t{}};
    right_map_t right{right_ext_t{}};
    EXPECT_EQ(left.stride(1), 8);
    EXPECT_TRUE(left.is_exhaustive());
    EXPECT_EQ(right.stride(0), 8);
    EXPECT_TRUE(right.is_exhaustive());
}

TEST(SubmdspanPaddedLayouts, OppositeRankTwoPaddedMappingIsNotConvertible) {
    using ext_t = std::extents<int, 3, 5>;
    using left_map_t = std::layout_left_padded<>::mapping<ext_t>;
    using right_map_t = std::layout_right_padded<4>::mapping<ext_t>;
    using rank1_t = std::extents<int, 5>;
    using left_rank1_t = std::layout_left_padded<>::mapping<rank1_t>;
    using right_rank1_t = std::layout_right_padded<4>::mapping<rank1_t>;

    static_assert(!std::is_constructible_v<left_map_t, right_map_t>);
    static_assert(std::is_constructible_v<left_rank1_t, right_rank1_t>);
}

TEST(SubmdspanPaddedLayouts, FullSlicePreservesPaddedLayout) {
    auto data = make_data<32>();
    using ext_t = std::extents<int, 3, 4>;
    using map_t = std::layout_left_padded<8>::mapping<ext_t>;
    std::mdspan<int, ext_t, std::layout_left_padded<8>> m(data.data(), map_t{ext_t{}, 8});

    auto sub = std::submdspan(m, std::full_extent, std::full_extent);
    EXPECT_TRUE((std::is_same_v<decltype(sub)::layout_type, std::layout_left_padded<8>>));
    EXPECT_EQ(sub.mapping().stride(1), 8);
    EXPECT_EQ((sub[2, 3]), 26);
}

// ---------------------------------------------------------------------------
// Feature-test macro
// ---------------------------------------------------------------------------

TEST(SubmdspanFeatureMacro, MacroDefined) {
    static_assert(__cpp_lib_constant_wrapper >= 202606L,
                  "__cpp_lib_constant_wrapper must expose the P4206 DR baseline");
    // If the native version ships, it will also define this macro.
    static_assert(__cpp_lib_submdspan >= 202603L,
                  "__cpp_lib_submdspan must be defined and >= 202603L");
}

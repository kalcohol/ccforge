#include <gtest/gtest.h>
#include <linalg>

#include <concepts>
#include <cstddef>
#include <mdspan>
#include <type_traits>
#include <utility>

namespace {

template<class Extents, class Layout = std::layout_right>
using transpose_mapping =
    typename std::linalg::layout_transpose<Layout>::template mapping<Extents>;

template<class Mapping>
inline constexpr bool returns_index_type = std::same_as<
    decltype(std::declval<const Mapping&>().required_span_size()),
    typename Mapping::index_type>;

template<class Index>
void expect_signed_index_type() {
    using fixed = std::extents<Index, 2, 3>;
    using dynamic = std::dextents<Index, 2>;
    using mixed = std::extents<Index, std::dynamic_extent, 3>;

    static_assert(!std::same_as<typename fixed::index_type, typename fixed::size_type>);
    static_assert(noexcept(std::declval<const transpose_mapping<fixed>&>()
                               .required_span_size()));
    EXPECT_TRUE((returns_index_type<transpose_mapping<fixed>>));
    EXPECT_TRUE((returns_index_type<transpose_mapping<dynamic>>));
    EXPECT_TRUE((returns_index_type<transpose_mapping<mixed>>));
    EXPECT_TRUE((returns_index_type<transpose_mapping<fixed, std::layout_left>>));
    EXPECT_TRUE((returns_index_type<transpose_mapping<fixed, std::layout_stride>>));
}

template<class Index>
constexpr bool standard_layout_controls() {
    using extents = std::extents<Index, 2, 3>;
    int data[] = {1, 2, 3, 4, 5, 6};
    const std::mdspan<int, extents, std::layout_right> right(data);
    const std::mdspan<int, extents, std::layout_left> left(data);
    const auto right_t = std::linalg::transposed(right);
    const auto left_t = std::linalg::transposed(left);

    static_assert(std::same_as<typename decltype(right_t)::layout_type, std::layout_left>);
    static_assert(std::same_as<typename decltype(left_t)::layout_type, std::layout_right>);
    static_assert(returns_index_type<typename decltype(right_t)::mapping_type>);
    static_assert(returns_index_type<typename decltype(left_t)::mapping_type>);
    return right_t.extent(0) == 3 && right_t.extent(1) == 2 &&
           left_t.extent(0) == 3 && left_t.extent(1) == 2 &&
           right_t.mapping().required_span_size() == 6 &&
           left_t.mapping().required_span_size() == 6 &&
           right_t[2, 1] == right[1, 2] && left_t[2, 1] == left[1, 2] &&
           right_t.mapping().stride(0) == 1 && right_t.mapping().stride(1) == 3 &&
           left_t.mapping().stride(0) == 2 && left_t.mapping().stride(1) == 1;
}

static_assert(standard_layout_controls<int>());
static_assert(standard_layout_controls<std::size_t>());

} // namespace

TEST(LinalgTransposeSpanSize, WrapperReturnsTheSignedIndexType) {
    expect_signed_index_type<short>();
    expect_signed_index_type<int>();
    expect_signed_index_type<long>();
    expect_signed_index_type<long long>();
    expect_signed_index_type<std::ptrdiff_t>();
}

TEST(LinalgTransposeSpanSize, UnsignedAndNestedMappingControlsRemainExact) {
    using unsigned_extents = std::extents<unsigned int, 2, 3>;
    using size_extents = std::extents<std::size_t, 2, 3>;
    using signed_extents = std::extents<int, 2, 3>;

    EXPECT_TRUE((returns_index_type<transpose_mapping<unsigned_extents>>));
    EXPECT_TRUE((returns_index_type<transpose_mapping<size_extents>>));
    EXPECT_TRUE((returns_index_type<std::layout_right::mapping<signed_extents>>));
    EXPECT_TRUE((returns_index_type<std::layout_left::mapping<signed_extents>>));
    EXPECT_TRUE((returns_index_type<std::layout_stride::mapping<signed_extents>>));
}

TEST(LinalgTransposeSpanSize, PublicStandardLayoutViewsPreserveTheirValues) {
    EXPECT_TRUE(standard_layout_controls<int>());
    EXPECT_TRUE(standard_layout_controls<std::size_t>());
}

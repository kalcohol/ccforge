#include <gtest/gtest.h>
#include <linalg>

#include <array>
#include <concepts>
#include <cstddef>
#include <limits>
#include <mdspan>
#include <type_traits>
#include <utility>

// Hosts that still check the pre-LWG 4603/4606 layout_stride preconditions:
// the MSVC STL in iterator-debug builds, and libc++ in a hardened mode (zig
// 0.14's bundled libc++ defaults to debug hardening). libc++ rejects zero or
// negative strides in every hardened mode and checks the old permutation rule
// from the extensive mode on. Leave the other portable cases enabled there.
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL != 0
#define FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES 1
#define FORGE_TEST_HOST_CHECKS_STRIDE_PERMUTATION 1
#elif defined(_LIBCPP_HARDENING_MODE) && defined(_LIBCPP_HARDENING_MODE_NONE) && \
    _LIBCPP_HARDENING_MODE != _LIBCPP_HARDENING_MODE_NONE
#define FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES 1
#if defined(_LIBCPP_HARDENING_MODE_FAST) && _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_FAST
#define FORGE_TEST_HOST_CHECKS_STRIDE_PERMUTATION 0
#else
#define FORGE_TEST_HOST_CHECKS_STRIDE_PERMUTATION 1
#endif
#else
#define FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES 0
#define FORGE_TEST_HOST_CHECKS_STRIDE_PERMUTATION 0
#endif

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

template<class Layout, class E>
constexpr bool check_padded_transpose(const typename Layout::template mapping<E>& source) {
    using mapping = typename Layout::template mapping<E>;
    std::array<int, 512> data{};
    const std::mdspan<int, E, Layout> a(data.data(), source);
    const auto t = std::linalg::transposed(a);
    const auto twice = std::linalg::transposed(t);
    using TE = typename decltype(t)::extents_type;
    using opposite_layout = std::conditional_t<
        std::same_as<Layout, std::layout_left_padded<mapping::padding_value>>,
        std::layout_right_padded<mapping::padding_value>,
        std::layout_left_padded<mapping::padding_value>>;
    static_assert(std::same_as<typename decltype(t)::layout_type, opposite_layout>);
    static_assert(TE::static_extent(0) == E::static_extent(1));
    static_assert(TE::static_extent(1) == E::static_extent(0));
    static_assert(std::same_as<typename decltype(twice)::mapping_type, mapping>);
    if (t.extent(0) != a.extent(1) || t.extent(1) != a.extent(0) ||
        t.stride(0) != a.stride(1) || t.stride(1) != a.stride(0) ||
        t.mapping().required_span_size() != source.required_span_size() ||
        !(twice.mapping() == source) || twice.data_handle() != a.data_handle())
        return false;
    for (std::size_t i = 0; i < a.extent(0); ++i)
        for (std::size_t j = 0; j < a.extent(1); ++j) {
            a[i, j] = int(100 * i + j);
            if (t[j, i] != a[i, j] || &t[j, i] != &a[i, j] ||
                &twice[i, j] != &a[i, j]) return false;
        }
    return true;
}

template<class Layout, class E>
constexpr bool padded_transpose(E e, bool default_padding = false) {
    using mapping = typename Layout::template mapping<E>;
    constexpr auto pad = mapping::padding_value == std::dynamic_extent
        ? std::size_t(8) : mapping::padding_value;
    const mapping source = default_padding ? mapping(e) : mapping(e, pad);
    return check_padded_transpose<Layout, E>(source);
}

template<class Layout, class E>
constexpr bool converted_padded_transpose(E e, typename E::index_type stride,
                                         int expected_span) {
    using index_type = typename E::index_type;
    using mapping = typename Layout::template mapping<E>;
    using strided_mapping = std::layout_stride::mapping<E>;
    constexpr bool left = std::same_as<Layout,
        std::layout_left_padded<mapping::padding_value>>;
    const std::array<index_type, 2> strides = left
        ? std::array<index_type, 2>{index_type(1), stride}
        : std::array<index_type, 2>{stride, index_type(1)};
    static_assert(std::is_constructible_v<mapping, const strided_mapping&>);
    static_assert(!std::is_convertible_v<const strided_mapping&, mapping>);
    const strided_mapping strided(e, strides);
    const mapping source(strided);
    return source.extents() == e &&
           source.stride(0) == strides[0] && source.stride(1) == strides[1] &&
           strided.required_span_size() == expected_span &&
           source.required_span_size() == expected_span &&
           check_padded_transpose<Layout, E>(source);
}

template<class Layout, class Index>
constexpr bool narrow_converted_transposes() {
    using mapping = typename Layout::template mapping<std::dextents<Index, 2>>;
    constexpr bool left = std::same_as<Layout,
        std::layout_left_padded<mapping::padding_value>>;
    // The full padded product is max(Index) + 1, while the span is three less.
    constexpr int outer = int((std::numeric_limits<Index>::max)()) / 8 + 1;
    constexpr int span = (outer - 1) * 8 + 5;
    static_assert(outer * 8 > int((std::numeric_limits<Index>::max)()));
    static_assert(span <= int((std::numeric_limits<Index>::max)()));
    using dynamic = std::dextents<Index, 2>;
    using mixed = std::conditional_t<left,
        std::extents<Index, 5, std::dynamic_extent>,
        std::extents<Index, std::dynamic_extent, 5>>;
    return converted_padded_transpose<Layout>(
               dynamic(left ? 5 : outer, left ? outer : 5), Index(8), span) &&
           converted_padded_transpose<Layout>(mixed(outer), Index(8), span);
}

template<class Index>
constexpr bool all_narrow_converted_transposes() {
    return narrow_converted_transposes<std::layout_left_padded<8>, Index>() &&
           narrow_converted_transposes<std::layout_right_padded<8>, Index>() &&
           narrow_converted_transposes<std::layout_left_padded<std::dynamic_extent>, Index>() &&
           narrow_converted_transposes<std::layout_right_padded<std::dynamic_extent>, Index>();
}

constexpr bool empty_converted_transposes() {
    using left = std::layout_left_padded<std::dynamic_extent>;
    using right = std::layout_right_padded<std::dynamic_extent>;
    using dynamic = std::dextents<int, 2>;
    // Static padding requires stride == LEAST-MULTIPLE-AT-LEAST(P, 0) == 0,
    // so a positive stride with a zero padded extent is only valid dynamically.
    for (int outer : {0, 1, 13})
        for (int stride : {1, 8, 16}) {
            if (!converted_padded_transpose<left>(dynamic(0, outer), stride, 0) ||
                !converted_padded_transpose<right>(dynamic(outer, 0), stride, 0))
                return false;
        }
    return converted_padded_transpose<left>(
               std::extents<int, std::dynamic_extent, 13>(0), 8, 0) &&
           converted_padded_transpose<right>(
               std::extents<int, 13, std::dynamic_extent>(0), 8, 0) &&
           // These valid reverse-order permutations have stride < padded extent.
           converted_padded_transpose<left>(dynamic(9, 0), 1, 0) &&
           converted_padded_transpose<right>(dynamic(0, 9), 1, 0) &&
           converted_padded_transpose<left>(dynamic(9, 1), 1, 9) &&
           converted_padded_transpose<right>(dynamic(1, 9), 1, 9);
}

static_assert(empty_converted_transposes());
static_assert(all_narrow_converted_transposes<unsigned char>());
static_assert(all_narrow_converted_transposes<signed char>());

constexpr bool static_zero_padding_transposes() {
    using left = std::layout_left_padded<0>;
    using right = std::layout_right_padded<0>;
    using left_extents = std::extents<int, 0, 3>;
    using right_extents = std::extents<int, 3, 0>;
    return check_padded_transpose<left, left_extents>(left::mapping<left_extents>(left_extents{})) &&
           check_padded_transpose<right, right_extents>(right::mapping<right_extents>(right_extents{}));
}

static_assert(static_zero_padding_transposes());

#if !FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES
constexpr bool imported_zero_stride_transposes() {
    using left = std::layout_left_padded<std::dynamic_extent>;
    using right = std::layout_right_padded<std::dynamic_extent>;
    using extents = std::dextents<int, 2>;
    // The padded extent is positive but the zero stride is unused.
    return converted_padded_transpose<left>(extents(3, 0), 0, 0) &&
           converted_padded_transpose<right>(extents(0, 3), 0, 0) &&
           converted_padded_transpose<left>(extents(3, 1), 0, 3) &&
           converted_padded_transpose<right>(extents(1, 3), 0, 3);
}

template<bool Left>
constexpr bool narrowed_unused_stride_transposes() {
    using index_t = signed char;
    using wide_extents = std::dextents<int, 2>;
    using narrow_extents = std::dextents<index_t, 2>;
    using narrow_strided = std::layout_stride::mapping<narrow_extents>;
    constexpr int stride = int((std::numeric_limits<index_t>::max)()) + 1;
    constexpr auto expected = static_cast<index_t>(stride);
    static_assert(expected < 0);
    const std::layout_stride::mapping<wide_extents> wide(
        wide_extents(Left ? 3 : 1, Left ? 1 : 3),
        Left ? std::array{1, stride} : std::array{stride, 1});
    const narrow_strided narrow(wide);
    int data[] = {1, 2, 3};
    const std::mdspan<int, narrow_extents, std::layout_stride> source(data, narrow);
    const auto t = std::linalg::transposed(source);
    if (narrow.stride(Left ? 1 : 0) != expected ||
        t.stride(Left ? 0 : 1) != expected || t.stride(Left ? 1 : 0) != 1 ||
        t.mapping().required_span_size() != 3 || &t[Left ? 0 : 2, Left ? 2 : 0] != data + 2) {
        return false;
    }
#if defined(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT) || !defined(FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS)
    // Exercise Forge's import. Native padded mappings may impose extra checks.
    using layout = std::conditional_t<Left, std::layout_left_padded<std::dynamic_extent>,
                                      std::layout_right_padded<std::dynamic_extent>>;
    const typename layout::template mapping<narrow_extents> padded(wide);
    return padded.stride(Left ? 1 : 0) == expected &&
           check_padded_transpose<layout, narrow_extents>(padded);
#else
    return true;
#endif
}

static_assert(imported_zero_stride_transposes());
static_assert(narrowed_unused_stride_transposes<true>());
static_assert(narrowed_unused_stride_transposes<false>());
#endif

template<class L>
constexpr bool fixed_padded_transposes() {
    return padded_transpose<L>(std::extents<int, 9, 13>{}) &&
           padded_transpose<L>(std::extents<int, 0, 13>{}) &&
           padded_transpose<L>(std::extents<int, 9, 0>{}) &&
           padded_transpose<L>(std::extents<int, 0, 0>{});
}

static_assert(fixed_padded_transposes<std::layout_left_padded<8>>());
static_assert(fixed_padded_transposes<std::layout_right_padded<8>>());
static_assert(fixed_padded_transposes<std::layout_left_padded<std::dynamic_extent>>());
static_assert(fixed_padded_transposes<std::layout_right_padded<std::dynamic_extent>>());

template<class L>
void dynamic_padded_transposes() {
    for (int m : {0, 9})
        for (int n : {0, 13})
            for (bool use_default : {false, true}) {
                EXPECT_TRUE(padded_transpose<L>(std::dextents<int, 2>(m, n), use_default));
                EXPECT_TRUE(padded_transpose<L>(
                    std::extents<int, std::dynamic_extent, 13>(m), use_default));
                EXPECT_TRUE(padded_transpose<L>(
                    std::extents<int, 9, std::dynamic_extent>(n), use_default));
            }
}

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

TEST(LinalgTransposeSpanSize, PaddedStaticAndDynamicMappingIdentity) {
    EXPECT_TRUE(static_zero_padding_transposes());
    EXPECT_TRUE(fixed_padded_transposes<std::layout_left_padded<8>>());
    EXPECT_TRUE(fixed_padded_transposes<std::layout_right_padded<8>>());
    EXPECT_TRUE(fixed_padded_transposes<std::layout_left_padded<std::dynamic_extent>>());
    EXPECT_TRUE(fixed_padded_transposes<std::layout_right_padded<std::dynamic_extent>>());
    dynamic_padded_transposes<std::layout_left_padded<8>>();
    dynamic_padded_transposes<std::layout_right_padded<8>>();
    dynamic_padded_transposes<std::layout_left_padded<std::dynamic_extent>>();
    dynamic_padded_transposes<std::layout_right_padded<std::dynamic_extent>>();
}

TEST(LinalgTransposeSpanSize, ConvertedPaddedEmptyMappingIdentity) {
    EXPECT_TRUE(empty_converted_transposes());
#if !FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES
    EXPECT_TRUE(imported_zero_stride_transposes());
#endif
}

TEST(LinalgTransposeSpanSize, ConvertedPaddedNarrowIndexMappingIdentity) {
    EXPECT_TRUE(all_narrow_converted_transposes<unsigned char>());
    EXPECT_TRUE(all_narrow_converted_transposes<signed char>());
#if !FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES
    EXPECT_TRUE(narrowed_unused_stride_transposes<true>());
    EXPECT_TRUE(narrowed_unused_stride_transposes<false>());
#endif
}

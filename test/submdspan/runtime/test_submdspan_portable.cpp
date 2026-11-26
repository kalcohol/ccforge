#include <mdspan>
#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

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

[[maybe_unused]] constexpr bool host_checks_positive_strides =
    FORGE_TEST_HOST_CHECKS_POSITIVE_STRIDES != 0;
[[maybe_unused]] constexpr bool host_checks_stride_permutation =
    FORGE_TEST_HOST_CHECKS_STRIDE_PERMUTATION != 0;

template<class Layout>
bool check_submdspan() {
    std::array<int, 32> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i + 1);
    }
    using extents_t = std::extents<std::size_t, 3, 4>;
    using index_t = extents_t::index_type;
    using mapping_t = typename Layout::template mapping<extents_t>;
    std::mdspan<int, extents_t, Layout> source(data.data(), mapping_t(extents_t{}));
    // LWG 4588: these canonical slice types also work on static axes.
    index_t row_index = 1;
    index_t column_index = 2;
    std::full_extent_t all = std::full_extent;
    auto full = std::submdspan(source, std::full_extent, std::full_extent);
    auto row = std::submdspan(source, row_index, all);
    auto column = std::submdspan(source, all, column_index);
    auto window = std::submdspan(source, std::pair{1, 3}, std::pair{1, 4});
    auto scalar = std::submdspan(source, 2, 3);

    static_assert(decltype(full)::rank() == 2);
    static_assert(decltype(row)::rank() == 1);
    static_assert(decltype(column)::rank() == 1);
    static_assert(decltype(row)::static_extent(0) == 4);
    static_assert(decltype(column)::static_extent(0) == 3);
    static_assert(decltype(window)::rank() == 2);
    static_assert(decltype(scalar)::rank() == 0);
    if (full.data_handle() != source.data_handle() ||
        full.extent(0) != 3 || full.extent(1) != 4 ||
        full.stride(0) != source.stride(0) ||
        full.stride(1) != source.stride(1)) {
        return false;
    }
    if (row.extent(0) != 4 || row.stride(0) != source.stride(1) ||
        row.data_handle() != data.data() + source.mapping()(1, 0)) {
        return false;
    }
    if (column.extent(0) != 3 || column.stride(0) != source.stride(0) ||
        column.data_handle() != data.data() + source.mapping()(0, 2)) {
        return false;
    }
    if (window.extent(0) != 2 || window.extent(1) != 3 ||
        window.stride(0) != source.stride(0) ||
        window.stride(1) != source.stride(1) ||
        window.data_handle() != data.data() + source.mapping()(1, 1)) {
        return false;
    }
    if (scalar.data_handle() != data.data() + source.mapping()(2, 3) ||
        scalar[] != source[2, 3]) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (column[i] != source[i, 2]) {
            return false;
        }
        for (std::size_t j = 0; j < 4; ++j) {
            if (full[i, j] != source[i, j]) {
                return false;
            }
        }
    }
    for (std::size_t j = 0; j < 4; ++j) {
        if (row[j] != source[1, j]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (window[i, j] != source[i + 1, j + 1]) {
                return false;
            }
        }
    }
    return true;
}

bool check_singleton_stride() {
    using extents_t = std::extents<int, 1, 4>;
    using mapping_t = std::layout_stride::mapping<extents_t>;
    const mapping_t mapping(extents_t{}, std::array{100, 1});
    if (mapping.required_span_size() != 4 || !mapping.is_unique() ||
        mapping(0, 3) != 3) {
        return false;
    }
#if !defined(_MSVC_STL_VERSION)
    // LWG 4562: the singleton-axis stride does not affect exhaustiveness.
    return mapping.is_exhaustive();
#else
    // The MSVC STL observer still uses the old rank-two stride formula.
    return true;
#endif
}

#if defined(FORGE_TEST_SUBMDSPAN_EXTENT_SLICE)
bool check_non_chain_strides() {
    int data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const std::mdspan<int, std::extents<int, 3, 4>> source(data);
    const auto sub = std::submdspan(source, std::full_extent, std::extent_slice{0, 2, 3});
    static_assert(std::is_same_v<typename decltype(sub)::layout_type, std::layout_stride>);
    // LWG 4606: 4 exceeds the first axis's maximum offset 3, not 3 * 2.
    if (sub.extent(0) != 3 || sub.extent(1) != 2 ||
        sub.stride(0) != 4 || sub.stride(1) != 3 || !sub.is_unique()) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (sub[i, j] != source[i, 3 * j] || &sub[i, j] != &source[i, 3 * j]) {
                return false;
            }
        }
    }
    return true;
}

bool check_empty_slice() {
    int data[12]{};
    const std::mdspan<int, std::extents<int, 3, 4>> source(data);
    const auto sub = std::submdspan(source, std::extent_slice{3, 0, 3}, std::full_extent);
    static_assert(std::is_same_v<typename decltype(sub)::layout_type, std::layout_stride>);
    // libc++ 19.1.7 needs the zero extent on the largest-stride axis.
    return sub.extent(0) == 0 && sub.extent(1) == 4 &&
           sub.data_handle() == data + 12 && sub.stride(0) == 4 && sub.stride(1) == 1 &&
           sub.mapping().required_span_size() == 0 && sub.is_exhaustive();
}

bool check_empty_zero_stride_slice() {
    int data[1]{};
    const std::mdspan<int, std::extents<int, 3, 0>> source(data);
    const auto sub = std::submdspan(source, std::full_extent, std::extent_slice{0, 0, 3});
    return sub.extent(0) == 3 && sub.extent(1) == 0 && sub.data_handle() == data &&
           sub.stride(0) == 0 && sub.stride(1) == 1 &&
           sub.mapping().required_span_size() == 0 && sub.is_exhaustive();
}

bool check_singleton_slice() {
    int data[12]{};
    const std::mdspan<int, std::extents<int, 3, 4>> source(data);
    const auto sub = std::submdspan(source, std::full_extent, std::extent_slice{1, 1, 3});
    // The stride factor is ignored for a singleton slice.
    return sub.extent(0) == 3 && sub.extent(1) == 1 &&
           sub.stride(0) == 4 && sub.stride(1) == 1 && &sub[2, 0] == &source[2, 1];
}
#endif

#if defined(FORGE_TEST_SUBMDSPAN_PADDED) || defined(FORGE_TEST_SUBMDSPAN_PARTIAL_PADDED)
template<class Layout>
using empty_padded_mapping = typename Layout::template mapping<std::dextents<int, 2>>;

template<class Layout, bool Left>
bool check_empty_padded_conversions() {
    using extents_t = std::dextents<int, 2>;
    using stride_t = std::layout_stride::mapping<extents_t>;
#if defined(FORGE_TEST_SUBMDSPAN_PARTIAL_PADDED)
    // Stand-aside only guarantees the names on a partial host. Test the
    // conversion if that surface actually exists (as on libstdc++ 16).
    if constexpr (!requires(const empty_padded_mapping<Layout>& mapping, const extents_t& extents) {
        empty_padded_mapping<Layout>{extents};
        stride_t{mapping};
        mapping.is_exhaustive();
    }) {
        return true;
    } else
#endif
    {
        using mapping_t = empty_padded_mapping<Layout>;
        const auto check = [](extents_t extents, int padding_stride, bool exhaustive) {
            const mapping_t padded(extents);
            if (padded.is_exhaustive() != exhaustive) {
                return false;
            }
            const stride_t strided(padded);
            return strided.extents() == extents &&
                   strided.stride(0) == (Left ? 1 : padding_stride) &&
                   strided.stride(1) == (Left ? padding_stride : 1) &&
                   strided.required_span_size() == 0 && strided.is_exhaustive();
        };
        // The padded observer still compares the padded extent with its stride.
        return check(Left ? extents_t(3, 0) : extents_t(0, 3), 8, false) &&
               (host_checks_positive_strides ||
                check(Left ? extents_t(0, 3) : extents_t(3, 0), 0, true));
    }
}
#endif

#if defined(FORGE_TEST_SUBMDSPAN_PADDED) && \
    (defined(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT) || !defined(FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS))
template<class Layout, bool Left>
bool check_narrow_padded_conversion() {
    using index_t = signed char;
    using wide_extents_t = std::dextents<int, 2>;
    using wide_mapping_t = typename Layout::template mapping<wide_extents_t>;
    using narrow_mapping_t = std::layout_stride::mapping<std::dextents<index_t, 2>>;
    constexpr int padding = int((std::numeric_limits<index_t>::max)()) + 1;
    constexpr auto expected = static_cast<index_t>(padding);
    static_assert(expected < 0);
    const wide_mapping_t padded(wide_extents_t(Left ? 3 : 1, Left ? 1 : 3), padding);
    // Call Forge's conversion operator, not the host converting constructor.
    const auto strided = padded.operator narrow_mapping_t();
    return strided.extents() == padded.extents() &&
           strided.stride(Left ? 0 : 1) == 1 && strided.stride(Left ? 1 : 0) == expected &&
           strided.required_span_size() == 3 && strided(Left ? 2 : 0, Left ? 0 : 2) == 2;
}
#endif

} // namespace

int main() {
    if (!check_submdspan<std::layout_left>()) {
        return 1;
    }
    if (!check_submdspan<std::layout_right>()) {
        return 2;
    }
#if defined(FORGE_TEST_SUBMDSPAN_PADDED)
    if (!check_submdspan<std::layout_left_padded<8>>()) {
        return 3;
    }
    if (!check_submdspan<std::layout_right_padded<8>>()) {
        return 4;
    }
#endif
    if (!check_singleton_stride()) {
        return 5;
    }
#if defined(FORGE_TEST_SUBMDSPAN_EXTENT_SLICE)
    if (!host_checks_stride_permutation && !check_non_chain_strides()) {
        return 6;
    }
    if (!check_empty_slice()) {
        return 7;
    }
    if (!host_checks_positive_strides && !check_empty_zero_stride_slice()) {
        return 8;
    }
    if (!check_singleton_slice()) {
        return 9;
    }
#endif
#if defined(FORGE_TEST_SUBMDSPAN_PADDED) || defined(FORGE_TEST_SUBMDSPAN_PARTIAL_PADDED)
    if (!check_empty_padded_conversions<std::layout_left_padded<8>, true>()) {
        return 10;
    }
    if (!check_empty_padded_conversions<std::layout_right_padded<8>, false>()) {
        return 11;
    }
#endif
#if defined(FORGE_TEST_SUBMDSPAN_PADDED) && \
    (defined(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT) || !defined(FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS))
    if (!host_checks_positive_strides &&
        (!check_narrow_padded_conversion<std::layout_left_padded<>, true>() ||
         !check_narrow_padded_conversion<std::layout_right_padded<>, false>())) {
        return 12;
    }
#endif
}

// Compile probe: API surface verification for std::submdspan
// Verifies all public types and functions are accessible via #include <mdspan>.
// This test must compile successfully; it does not need to run.

#include <mdspan>
#include <tuple>
#include <type_traits>
#include <utility>

// full_extent_t and full_extent must exist
static_assert(std::is_same_v<decltype(std::full_extent), const std::full_extent_t>);

// current draft range slice types are accessible
using es_t = std::extent_slice<int, int, int>;
static_assert(std::is_same_v<es_t::offset_type, int>);
static_assert(std::is_same_v<es_t::extent_type, int>);
static_assert(std::is_same_v<es_t::stride_type, int>);

using rs_t = std::range_slice<int, int>;
static_assert(std::is_same_v<decltype(rs_t{}.stride), std::constant_wrapper<1zu>>);

// constant_wrapper is part of the current submdspan surface
using cw_t = std::constant_wrapper<2zu>;
static_assert(cw_t::value == 2zu);
static_assert(std::cw<3zu> == std::constant_wrapper<3zu>{});

// submdspan_mapping_result is a template with .mapping and .offset members
using smr_t = std::submdspan_mapping_result<
    std::layout_left::mapping<std::extents<int, 4> > >;
static_assert(std::is_same_v<
    decltype(smr_t{}.mapping),
    std::layout_left::mapping<std::extents<int, 4> > >);

// subextents is callable
static void check_subextents() {
    std::extents<int, 3, 4> e;
    // integer index: rank-reducing
    auto e1 = std::subextents(e, 0, std::full_extent);
    static_assert(decltype(e1)::rank() == 1);
    // full_extent + full_extent: both preserved
    auto e2 = std::subextents(e, std::full_extent, std::full_extent);
    static_assert(decltype(e2)::rank() == 2);
    // pair range: one dim
    auto e3 = std::subextents(e, std::pair{0,2}, std::full_extent);
    static_assert(decltype(e3)::rank() == 2);
    auto e4 = std::subextents(e, std::full_extent, std::range_slice{0, 4, 2});
    static_assert(decltype(e4)::rank() == 2);
    auto c = std::canonical_slices(e, std::full_extent, std::range_slice{0, 4, 2});
    static_assert(std::tuple_size_v<decltype(c)> == 2);
    (void)e1; (void)e2; (void)e3; (void)e4; (void)c;
}

// submdspan is callable
static void check_submdspan() {
    int data[12]{};
    std::mdspan<int, std::dextents<int,2>> m(data, 3, 4);
    // index + full_extent → rank-1
    auto s1 = std::submdspan(m, 1, std::full_extent);
    static_assert(s1.rank() == 1);
    // full_extent + full_extent → rank-2, layout_right preserved
    auto s2 = std::submdspan(m, std::full_extent, std::full_extent);
    static_assert(s2.rank() == 2);
    // current extent_slice: extent is the output count
    auto s3 = std::submdspan(m, 0, std::extent_slice{0, 2, 2});
    static_assert(s3.rank() == 1);
    // range_slice → rank-1
    auto s4 = std::submdspan(m, 0, std::range_slice{0, 4, 2});
    static_assert(s4.rank() == 1);
    // all index → rank-0
    auto s5 = std::submdspan(m, 1, 2);
    static_assert(s5.rank() == 0);
    (void)s1; (void)s2; (void)s3; (void)s4; (void)s5;
}

static void check_padded_layouts() {
    using ext_t = std::extents<int, 3, 4>;
    ext_t e;
    std::layout_left_padded<8>::mapping<ext_t> left(e, 8);
    std::layout_right_padded<8>::mapping<ext_t> right(e, 8);
    static_assert(decltype(left)::padding_value == 8);
    static_assert(decltype(right)::padding_value == 8);
    (void)left(2, 3);
    (void)right(2, 3);
}

int main() { return 0; }

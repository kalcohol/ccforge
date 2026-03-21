// Compile probe: API surface verification for std::submdspan
// Verifies all public types and functions are accessible via #include <mdspan>.
// This test must compile successfully; it does not need to run.

#include <mdspan>

// full_extent_t and full_extent must exist
static_assert(std::is_same_v<decltype(std::full_extent), const std::full_extent_t>);

// strided_slice is accessible
using ss_t = std::strided_slice<int, int, int>;
static_assert(std::is_same_v<ss_t::offset_type, int>);
static_assert(std::is_same_v<ss_t::extent_type, int>);
static_assert(std::is_same_v<ss_t::stride_type, int>);

// strided_slice with integral_constant fields (compile-time stride)
using ss_ct = std::strided_slice<int, std::integral_constant<int,4>, std::integral_constant<int,2>>;
static_assert(std::is_same_v<ss_ct::stride_type, std::integral_constant<int,2>>);

// submdspan_mapping_result is a template with .mapping and .offset members
using smr_t = std::submdspan_mapping_result<
    std::layout_left::mapping<std::extents<int, 4> > >;
static_assert(std::is_same_v<
    decltype(smr_t{}.mapping),
    std::layout_left::mapping<std::extents<int, 4> > >);

// submdspan_extents is callable
static void check_submdspan_extents() {
    std::extents<int, 3, 4> e;
    // integer index: rank-reducing
    auto e1 = std::submdspan_extents(e, 0, std::full_extent);
    static_assert(decltype(e1)::rank() == 1);
    // full_extent + full_extent: both preserved
    auto e2 = std::submdspan_extents(e, std::full_extent, std::full_extent);
    static_assert(decltype(e2)::rank() == 2);
    // pair range: one dim
    auto e3 = std::submdspan_extents(e, std::pair{0,2}, std::full_extent);
    static_assert(decltype(e3)::rank() == 2);
    (void)e1; (void)e2; (void)e3;
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
    // strided_slice → rank-1
    auto s3 = std::submdspan(m, 0, std::strided_slice{0, 4, 2});
    static_assert(s3.rank() == 1);
    // all index → rank-0
    auto s4 = std::submdspan(m, 1, 2);
    static_assert(s4.rank() == 0);
    (void)s1; (void)s2; (void)s3; (void)s4;
}

int main() { return 0; }

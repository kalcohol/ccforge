// Compile probe: transparent injection via #include <mdspan>
// Verifies that Forge's backport/mdspan wrapper correctly injects submdspan
// when the toolchain lacks it, and that the standard include path is unchanged.
//
// This test must compile successfully; it does not run.

#include <mdspan>
#include <type_traits>

// After including <mdspan>, std::submdspan and related types must be visible.
static_assert(sizeof(std::extent_slice<int,int,int>) > 0,
              "extent_slice must be available after #include <mdspan>");
static_assert(sizeof(std::range_slice<int,int>) > 0,
              "range_slice must be available after #include <mdspan>");
static_assert(sizeof(std::layout_left_padded<>) > 0,
              "layout_left_padded must be available after #include <mdspan>");

// submdspan_mapping_result must be a template
static_assert(sizeof(std::submdspan_mapping_result<
                         std::layout_left::mapping<std::dextents<int,1>>>) > 0,
              "submdspan_mapping_result must be available after #include <mdspan>");

// full_extent_t must be present (either from C++26 libc++ or our backport)
static_assert(std::is_same_v<decltype(std::full_extent), const std::full_extent_t>,
              "full_extent must have type full_extent_t");

// submdspan must be callable
static void check_submdspan() {
    int data[12]{};
    std::mdspan<int, std::dextents<int,2>> m(data, 3, 4);
    auto sub = std::submdspan(m, 1, std::full_extent);
    static_assert(sub.rank() == 1, "rank-1 submdspan from rank-2 with one index slice");
    auto e = std::subextents(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
    static_assert(decltype(e)::rank() == 2, "range_slice subextents stays rank-2");
    (void)sub;
    (void)e;
}

int main() { return 0; }

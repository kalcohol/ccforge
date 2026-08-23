# std::submdspan (P2630 + P3663/P3982 wording) is only meaningful when <mdspan>
# exists. Partial probes must look for submdspan-specific symbols: <mdspan> alone
# (for example GCC 14) has no submdspan and must still get the backport.
check_cxx_source_compiles("
    #include <mdspan>
    #include <tuple>
    int main() {
        int data[12]{};
        std::mdspan<int, std::extents<int, 3, 4>> m(data);
        auto sub = std::submdspan(m, 1, std::full_extent);
        auto e = std::subextents(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
        auto c = std::canonical_slices(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
        using es = std::extent_slice<int, int, int>;
        using rs = std::range_slice<int, int>;
        static_assert(__cpp_lib_submdspan >= 202603L);
        (void)sub; (void)e; (void)c; (void)sizeof(es); (void)sizeof(rs);
        return 0;
    }
" FORGE_SUBMDSPAN_FULL)
check_cxx_source_compiles("
    #include <mdspan>
    #ifndef __cpp_lib_submdspan
    #error no submdspan feature macro
    #endif
    int main() { return 0; }
" FORGE_SUBMDSPAN_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <mdspan>
    using probe = std::extent_slice<int, int, int>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_CURRENT)
check_cxx_source_compiles("
    #include <mdspan>
    using probe = std::strided_slice<int, int, int>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_LEGACY)
check_cxx_source_compiles("
    #include <mdspan>
    template<template<class> class>
    struct accepts_result_template {};
    using probe = accepts_result_template<std::submdspan_mapping_result>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT)
check_cxx_source_compiles("
    #include <mdspan>
    int main() {
        int data[1]{};
        std::mdspan<int, std::extents<int, 1>> view(data);
        auto sub = std::submdspan(view, std::full_extent);
        (void)sub;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_FUNCTION)
set(FORGE_SUBMDSPAN_PARTIAL FALSE)
if(FORGE_SUBMDSPAN_PARTIAL_MACRO
        OR FORGE_SUBMDSPAN_PARTIAL_CURRENT
        OR FORGE_SUBMDSPAN_PARTIAL_LEGACY
        OR FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT
        OR FORGE_SUBMDSPAN_PARTIAL_FUNCTION)
    set(FORGE_SUBMDSPAN_PARTIAL TRUE)
endif()
_forge_decide("std::submdspan" SUBMDSPAN FORGE_SUBMDSPAN_FULL FORGE_SUBMDSPAN_PARTIAL)

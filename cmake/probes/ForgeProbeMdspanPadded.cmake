# C++26 mdspan padded layouts (P2642). These are not useful only to submdspan,
# so they get a native guard before the submdspan wrapper can inject mappings.
check_cxx_source_compiles("
    #include <mdspan>
    #if !defined(__cpp_lib_mdspan) || __cpp_lib_mdspan < 202406L
    #error incomplete mdspan padded layouts
    #endif
    int main() {
        using ext_t = std::extents<int, 3, 4>;
        ext_t e;
        using dynamic_left = std::layout_left_padded<>;
        using dynamic_right = std::layout_right_padded<>;
        std::layout_left_padded<8>::mapping<ext_t> left(e, 8);
        std::layout_right_padded<8>::mapping<ext_t> right(e, 8);
        dynamic_left::mapping<ext_t> left_dynamic(e, 8);
        dynamic_right::mapping<ext_t> right_dynamic(e, 8);
        static_assert(decltype(left)::padding_value == 8);
        static_assert(decltype(right)::padding_value == 8);
        static_assert(decltype(left_dynamic)::padding_value == std::dynamic_extent);
        static_assert(decltype(right_dynamic)::padding_value == std::dynamic_extent);
        static_assert(decltype(left)::is_always_unique());
        static_assert(decltype(left)::is_always_strided());
        static_assert(decltype(right)::is_always_unique());
        static_assert(decltype(right)::is_always_strided());
        return static_cast<int>(
            left.stride(1) + right.stride(0) +
            left_dynamic.stride(1) + right_dynamic.stride(0) +
            left.required_span_size() + right.required_span_size() +
            left(1, 1) + right(1, 1) +
            left.is_unique() + right.is_strided());
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_FULL)
check_cxx_source_compiles("
    #include <mdspan>
    #include <cstddef>
    template<template<std::size_t> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::layout_left_padded>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT)
check_cxx_source_compiles("
    #include <mdspan>
    #include <cstddef>
    template<template<std::size_t> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::layout_right_padded>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT)
set(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL FALSE)
if(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT
        OR FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT)
    set(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL TRUE)
endif()
_forge_decide("std::mdspan padded layouts" MDSPAN_PADDED_LAYOUTS FORGE_MDSPAN_PADDED_LAYOUTS_FULL FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL)

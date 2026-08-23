# std::linalg (P1673). As with <simd>, an installed but declaration-free header
# is not partial native support at the configured language standard.
check_cxx_source_compiles("
    #include <linalg>
    #include <mdspan>
    #if !defined(__cpp_lib_linalg) || __cpp_lib_linalg < 202511L
    #error incomplete linalg surface
    #endif
    int main() {
        double vector_data[2] = {3, 4};
        double matrix_data[4] = {1, 2, 3, 4};
        double output_data[4]{};
        std::mdspan<double, std::extents<int, 2>> v(vector_data);
        std::mdspan<double, std::extents<int, 2, 2>> matrix(matrix_data);
        std::mdspan<double, std::extents<int, 2, 2>> output(output_data);
        auto g = std::linalg::setup_givens_rotation(3.0, 4.0);
        std::linalg::matrix_vector_product(matrix, v, v);
        std::linalg::matrix_product(matrix, matrix, output);
        using packed_layout = std::linalg::layout_blas_packed<
            std::linalg::upper_triangle_t,
            std::linalg::column_major_t>;
        packed_layout::mapping<std::extents<int, 2, 2>> packed(
            std::extents<int, 2, 2>{});
        return static_cast<int>(
            std::linalg::vector_two_norm(v) + g.r + packed.required_span_size());
    }
" FORGE_LINALG_FULL)
check_cxx_source_compiles("
    #include <linalg>
    #ifndef __cpp_lib_linalg
    #error no linalg feature macro
    #endif
    int main() { return 0; }
" FORGE_LINALG_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <linalg>
    using probe = std::linalg::setup_givens_rotation_result<double>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_SETUP)
check_cxx_source_compiles("
    #include <linalg>
    using probe = std::linalg::upper_triangle_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_TAG)
check_cxx_source_compiles("
    #include <linalg>
    template<template<class, class> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::linalg::layout_blas_packed>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_LAYOUT)
check_cxx_source_compiles("
    #include <linalg>
    #include <mdspan>
    int main() {
        double data[1]{};
        std::mdspan<double, std::extents<int, 1, 1>> matrix(data);
        std::linalg::matrix_product(matrix, matrix, matrix);
        return 0;
    }
" FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)
set(FORGE_LINALG_PARTIAL FALSE)
if(FORGE_LINALG_PARTIAL_MACRO
        OR FORGE_LINALG_PARTIAL_SETUP
        OR FORGE_LINALG_PARTIAL_TAG
        OR FORGE_LINALG_PARTIAL_LAYOUT
        OR FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)
    set(FORGE_LINALG_PARTIAL TRUE)
endif()
_forge_decide("std::linalg" LINALG FORGE_LINALG_FULL FORGE_LINALG_PARTIAL)

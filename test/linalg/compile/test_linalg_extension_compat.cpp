#include <forge/linalg.hpp>

#if !defined(FORGE_LINALG_BACKPORT_ACTIVE)
#error "This probe requires the linalg backport"
#endif

// Includes remain unsuppressed to detect header-internal deprecation warnings.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
int main() {
    int integers[]{3, 4};
    std::mdspan v(integers, std::extents<int, 2>{});
    if (std::linalg::vector_two_norm(v) != forge::vector_two_norm(v) ||
        std::linalg::vector_two_norm(v, 0) != 5 ||
        std::linalg::vector_two_norm(v, 0.0) != 5.0) return 5;
    double storage[]{1, 0, 0, 1}, xdata[]{1, 2}, output[4]{};
    std::mdspan matrix(storage, std::extents<int, 2, 2>{});
    std::mdspan x(xdata, std::extents<int, 2>{});
    std::mdspan out(output, std::extents<int, 2, 2>{});
    const auto t = std::linalg::upper_triangle;
    std::linalg::hermitian_matrix_rank_1_update(t, x, out);
    if (out[0, 0] != 1 || out[0, 1] != 2 || out[1, 1] != 4) return 6;
    std::linalg::hermitian_matrix_rank_2_update(t, x, x, out);
    if (out[0, 1] != 4) return 7;
    std::linalg::hermitian_matrix_product(t, matrix, matrix, out);
    if (out[0, 0] != 1 || out[1, 1] != 1) return 8;
    std::linalg::hermitian_matrix_rank_k_update(t, matrix, out);
    if (out[0, 0] != 1 || out[1, 1] != 1) return 9;
    return 0;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <forge/linalg.hpp>
#include <complex>
int check_forge() {
    int data[]{3, 4}; double fp[]{3, 4};
    std::mdspan a(data, std::extents<int, 1, 2>{});
    std::mdspan v(data, std::extents<int, 2>{});
    std::mdspan f(fp, std::extents<int, 2>{});
    if (forge::matrix_frob_norm(a) != 5 || forge::matrix_frob_norm(a, 0.0) != 5) return 1;
    auto ssq = forge::vector_sum_of_squares(v, forge::sum_of_squares_result<int>{0, 1});
    if (ssq.scaling_factor != 1 || ssq.scaled_sum_of_squares != 25) return 2;
    if (forge::setup_givens_rotation(3, 0).r != 3) return 3;
    static_assert(noexcept(forge::setup_givens_rotation(3, 0)));
    forge::apply_givens_rotation(v, f, 1, 0);
    // Standard integer one/inf norms and floating-point paths must stay warning-free.
    (void)std::linalg::matrix_one_norm(a);
    (void)std::linalg::matrix_inf_norm(a);
    (void)std::linalg::vector_two_norm(f);
    (void)std::linalg::setup_givens_rotation(3.0, 0.0);
    std::mdspan af(fp, std::extents<int, 1, 2>{});
    (void)std::linalg::matrix_frob_norm(af);
    (void)std::linalg::vector_sum_of_squares(f, std::linalg::sum_of_squares_result<double>{0, 1});
    return data[0] != 3 || data[1] != 4;
}
#if !defined(FORGE_DEPRECATION_CASE)
#define FORGE_DEPRECATION_CASE 0
#endif
#if FORGE_DEPRECATION_CASE == 0
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
int main() {
    int data[]{3, 4}; double fp[]{3, 4};
    std::mdspan a(data, std::extents<int, 1, 2>{});
    std::mdspan af(fp, std::extents<int, 1, 2>{});
    std::mdspan v(data, std::extents<int, 2>{});
    std::mdspan f(fp, std::extents<int, 2>{});
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
    if (std::linalg::matrix_frob_norm(a) != forge::matrix_frob_norm(a)) return 4;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
    if (std::linalg::matrix_frob_norm(a, 0) != 5) return 5;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 3
    if (std::linalg::matrix_frob_norm(a, 0.0) != 5) return 6;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 4
    if (std::linalg::matrix_frob_norm(af, 0) != 5) return 7;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 5
    if (std::linalg::vector_sum_of_squares(v, std::linalg::sum_of_squares_result<int>{0, 1}).scaled_sum_of_squares != 25) return 8;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 6
    (void)std::linalg::vector_sum_of_squares(v, std::linalg::sum_of_squares_result<double>{0, 1});
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 7
    (void)std::linalg::vector_sum_of_squares(f, std::linalg::sum_of_squares_result<int>{0, 1});
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 8
    if (std::linalg::setup_givens_rotation(3, 0).r != 3) return 9;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 9
    std::linalg::apply_givens_rotation(v, f, 1, 0);
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 10
    (void)std::linalg::vector_two_norm(v);
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 11
    (void)std::linalg::vector_two_norm(v, 0.0);
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 12
    (void)std::linalg::vector_two_norm(f, 0);
#endif
    return check_forge();
}
#if FORGE_DEPRECATION_CASE == 0
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

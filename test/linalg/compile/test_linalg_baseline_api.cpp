// Compile probe: current Forge std::linalg baseline before conformance slices.
// This intentionally records APIs that later linalg taskbooks are expected to
// rename or tighten.

#include <complex>
#include <linalg>
#include <mdspan>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::linalg::upper_triangle),
                             const std::linalg::upper_triangle_t>);
static_assert(std::is_same_v<decltype(std::linalg::lower_triangle),
                             const std::linalg::lower_triangle_t>);
static_assert(std::is_same_v<decltype(std::linalg::explicit_diagonal),
                             const std::linalg::explicit_diagonal_t>);
static_assert(std::is_same_v<decltype(std::linalg::implicit_unit_diagonal),
                             const std::linalg::implicit_unit_diagonal_t>);

static void check_level1_and_helpers()
{
    double x_storage[3]{1.0, 2.0, 3.0};
    double y_storage[3]{4.0, 5.0, 6.0};
    double z_storage[3]{};
    double matrix_storage[6]{};

    std::mdspan x(x_storage, std::extents<int, 3>{});
    std::mdspan y(y_storage, std::extents<int, 3>{});
    std::mdspan z(z_storage, std::extents<int, 3>{});
    std::mdspan matrix(matrix_storage, std::extents<int, 2, 3>{});

    std::linalg::copy(x, z);
    std::linalg::scale(2.0, z);
    std::linalg::swap_elements(x, y);
    std::linalg::add(x, y, z);

    (void)std::linalg::dot(x, y);
    (void)std::linalg::dotc(x, y);
    (void)std::linalg::vector_two_norm(x);
    (void)std::linalg::vector_abs_sum(x);
    (void)std::linalg::vector_idx_abs_max(x);
    (void)std::linalg::vector_sum_of_squares(
        x, std::linalg::sum_of_squares_result<double>{});
    (void)std::linalg::matrix_frob_norm(matrix);
    (void)std::linalg::matrix_one_norm(matrix);
    (void)std::linalg::matrix_inf_norm(matrix);

    (void)std::linalg::scaled(3.0, x);
    (void)std::linalg::transposed(matrix);
    (void)std::linalg::conjugated(x);
    (void)std::linalg::conjugate_transposed(matrix);
}

static void check_current_givens_names()
{
    double x_storage[2]{1.0, 2.0};
    double y_storage[2]{3.0, 4.0};
    std::mdspan x(x_storage, std::extents<int, 2>{});
    std::mdspan y(y_storage, std::extents<int, 2>{});

    auto rotation = std::linalg::givens_rotation_setup(3.0, 4.0);
    static_assert(std::is_same_v<decltype(rotation),
                                 std::linalg::givens_rotation_result<double>>);
    std::linalg::givens_rotation_apply(x, y, rotation.c, rotation.s);
}

static void check_level2_current_shapes()
{
    double matrix_storage[4]{};
    double x_storage[2]{1.0, 2.0};
    double y_storage[2]{3.0, 4.0};
    double z_storage[2]{};

    std::mdspan matrix(matrix_storage, std::extents<int, 2, 2>{});
    std::mdspan x(x_storage, std::extents<int, 2>{});
    std::mdspan y(y_storage, std::extents<int, 2>{});
    std::mdspan z(z_storage, std::extents<int, 2>{});

    std::linalg::matrix_vector_product(matrix, x, z);
    std::linalg::matrix_vector_product(matrix, x, y, z);
    std::linalg::triangular_matrix_vector_product(
        matrix, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x);
    std::linalg::triangular_matrix_vector_product(
        matrix, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x, z);
    std::linalg::triangular_matrix_vector_solve(
        matrix, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x);
    std::linalg::symmetric_matrix_vector_product(
        matrix, std::linalg::upper_triangle, x, z);
    std::linalg::symmetric_matrix_vector_product(
        matrix, std::linalg::upper_triangle, x, y, z);
    std::linalg::matrix_rank_1_update(x, y, matrix);
    std::linalg::symmetric_matrix_rank_1_update(
        2.0, std::linalg::upper_triangle, x, matrix);
    std::linalg::symmetric_matrix_rank_2_update(
        2.0, std::linalg::upper_triangle, x, y, matrix);
}

static void check_complex_hermitian_current_shapes()
{
    using complex = std::complex<double>;
    complex matrix_storage[4]{};
    complex x_storage[2]{};
    complex y_storage[2]{};

    std::mdspan matrix(matrix_storage, std::extents<int, 2, 2>{});
    std::mdspan x(x_storage, std::extents<int, 2>{});
    std::mdspan y(y_storage, std::extents<int, 2>{});

    std::linalg::hermitian_matrix_vector_product(
        matrix, std::linalg::upper_triangle, x, y);
    std::linalg::matrix_rank_1_update_c(x, y, matrix);
    std::linalg::hermitian_matrix_rank_1_update(
        std::linalg::upper_triangle, x, matrix);
    std::linalg::hermitian_matrix_rank_2_update(
        std::linalg::upper_triangle, x, y, matrix);
}

static void check_level3_current_shapes()
{
    double a_storage[4]{};
    double b_storage[4]{};
    double c_storage[4]{};
    double e_storage[4]{};

    std::mdspan a(a_storage, std::extents<int, 2, 2>{});
    std::mdspan b(b_storage, std::extents<int, 2, 2>{});
    std::mdspan c(c_storage, std::extents<int, 2, 2>{});
    std::mdspan e(e_storage, std::extents<int, 2, 2>{});

    std::linalg::matrix_product(a, b, c);
    std::linalg::matrix_product(a, b, e, c);
    std::linalg::triangular_matrix_product(
        a, std::linalg::upper_triangle, std::linalg::explicit_diagonal,
        std::linalg::column_major, b, c);
    std::linalg::triangular_matrix_matrix_left_solve(
        a, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b);
    std::linalg::symmetric_matrix_product(
        a, std::linalg::upper_triangle, b, c);
    std::linalg::symmetric_matrix_rank_k_update(
        2.0, std::linalg::upper_triangle, a, c);
    std::linalg::symmetric_matrix_rank_2k_update(
        2.0, std::linalg::upper_triangle, a, b, c);
}

int main()
{
    check_level1_and_helpers();
    check_current_givens_names();
    check_level2_current_shapes();
    check_complex_hermitian_current_shapes();
    check_level3_current_shapes();
}

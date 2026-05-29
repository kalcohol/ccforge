#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <complex>

namespace {

using complex = std::complex<double>;

void expect_complex_near(complex actual, complex expected)
{
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

} // namespace

TEST(LinalgLevel3Gemm, RectangularAndUpdateForms) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double b_data[] = {7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    double e_data[] = {10.0, 10.0, 10.0, 10.0};
    double c_data[] = {0.0, 0.0, 0.0, 0.0};
    std::mdspan A(a_data, std::extents<int, 2, 3>{});
    std::mdspan B(b_data, std::extents<int, 3, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    std::linalg::matrix_product(A, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 58.0);
    EXPECT_DOUBLE_EQ((C[0, 1]), 64.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 139.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 154.0);

    std::linalg::matrix_product(A, B, E, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 68.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 164.0);
}

TEST(LinalgLevel3Gemm, LayoutLeftAndLayoutStrideInputs) {
    double left_a_data[6]{};
    double left_b_data[6]{};
    double c_data[] = {0.0, 0.0, 0.0, 0.0};
    std::mdspan<double, std::extents<int, 2, 3>, std::layout_left> A(left_a_data);
    std::mdspan<double, std::extents<int, 3, 2>, std::layout_left> B(left_b_data);
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    A[0, 0] = 1.0; A[0, 1] = 2.0; A[0, 2] = 3.0;
    A[1, 0] = 4.0; A[1, 1] = 5.0; A[1, 2] = 6.0;
    B[0, 0] = 7.0; B[0, 1] = 8.0;
    B[1, 0] = 9.0; B[1, 1] = 10.0;
    B[2, 0] = 11.0; B[2, 1] = 12.0;
    std::linalg::matrix_product(A, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 58.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 154.0);

    double stride_a_data[] = {1.0, 2.0, 3.0, -100.0, 4.0, 5.0, 6.0};
    using a_extents_t = std::extents<int, 2, 3>;
    using a_mapping_t = std::layout_stride::mapping<a_extents_t>;
    a_mapping_t a_mapping(a_extents_t{}, std::array<int, 2>{4, 1});
    std::mdspan<double, a_extents_t, std::layout_stride> stride_a(stride_a_data, a_mapping);
    std::linalg::matrix_product(stride_a, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 58.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 154.0);
}

TEST(LinalgLevel3TriangularProduct, UpperLowerAndImplicitUnitDiagonal) {
    double upper_data[] = {1.0, 2.0, 3.0, 100.0, 4.0, 5.0, 100.0, 100.0, 6.0};
    double lower_data[] = {1.0, 100.0, 100.0, 2.0, 3.0, 100.0, 4.0, 5.0, 6.0};
    double b_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double c_data[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan B(b_data, std::extents<int, 3, 2>{});
    std::mdspan C(c_data, std::extents<int, 3, 2>{});

    std::linalg::triangular_matrix_product(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal,
        std::linalg::column_major, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 37.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 36.0);

    std::linalg::triangular_matrix_product(
        lower, std::linalg::lower_triangle, std::linalg::explicit_diagonal,
        std::linalg::column_major, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 1.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 16.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 64.0);

    double unit_data[] = {99.0, 2.0, 3.0, 100.0, 99.0, 5.0, 100.0, 100.0, 99.0};
    std::mdspan unit(unit_data, std::extents<int, 3, 3>{});
    std::linalg::triangular_matrix_product(
        unit, std::linalg::upper_triangle, std::linalg::implicit_unit_diagonal,
        std::linalg::column_major, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 28.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 6.0);
}

TEST(LinalgLevel3TriangularSolve, LeftSolveMultipleRightHandSides) {
    double upper_data[] = {2.0, 1.0, -1.0, 100.0, 3.0, 2.0, 100.0, 100.0, 4.0};
    double rhs_data[] = {0.0, 2.0, 19.0, 24.0, 20.0, 24.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan rhs(rhs_data, std::extents<int, 3, 2>{});

    std::linalg::triangular_matrix_matrix_left_solve(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal, rhs);
    EXPECT_DOUBLE_EQ((rhs[0, 0]), 1.0);
    EXPECT_DOUBLE_EQ((rhs[0, 1]), 2.0);
    EXPECT_DOUBLE_EQ((rhs[2, 0]), 5.0);
    EXPECT_DOUBLE_EQ((rhs[2, 1]), 6.0);
}

TEST(LinalgLevel3SymmetricProduct, UpperAndLowerUseOnlySelectedTriangle) {
    double upper_data[] = {1.0, 2.0, 3.0, 100.0, 4.0, 5.0, 100.0, 100.0, 6.0};
    double lower_data[] = {1.0, 100.0, 100.0, 2.0, 4.0, 100.0, 3.0, 5.0, 6.0};
    double b_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double c_data[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan B(b_data, std::extents<int, 3, 2>{});
    std::mdspan C(c_data, std::extents<int, 3, 2>{});

    std::linalg::symmetric_matrix_product(upper, std::linalg::upper_triangle, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 50.0);
    EXPECT_DOUBLE_EQ((C[2, 0]), 48.0);

    std::linalg::symmetric_matrix_product(lower, std::linalg::lower_triangle, B, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 50.0);
    EXPECT_DOUBLE_EQ((C[2, 0]), 48.0);
}

TEST(LinalgLevel3HermitianProduct, IgnoresDiagonalImaginaryAndSupportsLower) {
    complex upper_data[] = {{2.0, 99.0}, {1.0, 1.0}, {99.0, 99.0}, {3.0, -88.0}};
    complex lower_data[] = {{2.0, 99.0}, {99.0, 99.0}, {1.0, -1.0}, {3.0, -88.0}};
    complex b_data[] = {{1.0, 1.0}, {2.0, 0.0}, {3.0, -1.0}, {4.0, 1.0}};
    complex c_data[] = {{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}};
    std::mdspan upper(upper_data, std::extents<int, 2, 2>{});
    std::mdspan lower(lower_data, std::extents<int, 2, 2>{});
    std::mdspan B(b_data, std::extents<int, 2, 2>{});
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    std::linalg::hermitian_matrix_product(upper, std::linalg::upper_triangle, B, C);
    expect_complex_near(C[0, 0], {6.0, 4.0});
    expect_complex_near(C[0, 1], {7.0, 5.0});
    expect_complex_near(C[1, 0], {11.0, -3.0});
    expect_complex_near(C[1, 1], {14.0, 1.0});

    std::linalg::hermitian_matrix_product(lower, std::linalg::lower_triangle, B, C);
    expect_complex_near(C[0, 0], {6.0, 4.0});
    expect_complex_near(C[1, 1], {14.0, 1.0});
}

TEST(LinalgLevel3RankUpdate, SymmetricRankKOverwriteAndUpdate) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double e_data[] = {10.0, 10.0, 10.0, 10.0};
    double c_data[] = {99.0, 99.0, 99.0, 99.0};
    std::mdspan A(a_data, std::extents<int, 2, 3>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    std::linalg::symmetric_matrix_rank_k_update(2.0, A, C, std::linalg::upper_triangle);
    EXPECT_DOUBLE_EQ((C[0, 0]), 28.0);
    EXPECT_DOUBLE_EQ((C[0, 1]), 64.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 99.0);

    std::linalg::symmetric_matrix_rank_k_update(2.0, A, E, C, std::linalg::lower_triangle);
    EXPECT_DOUBLE_EQ((C[1, 0]), 74.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 164.0);
}

TEST(LinalgLevel3RankUpdate, SymmetricRank2KOverwriteAndUpdate) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0};
    double b_data[] = {5.0, 6.0, 7.0, 8.0};
    double e_data[] = {10.0, 10.0, 10.0, 10.0};
    double c_data[] = {99.0, 99.0, 99.0, 99.0};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan B(b_data, std::extents<int, 2, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    std::linalg::symmetric_matrix_rank_2k_update(A, B, C, std::linalg::upper_triangle);
    EXPECT_DOUBLE_EQ((C[0, 0]), 34.0);
    EXPECT_DOUBLE_EQ((C[0, 1]), 62.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 99.0);

    std::linalg::symmetric_matrix_rank_2k_update(A, B, E, C, std::linalg::lower_triangle);
    EXPECT_DOUBLE_EQ((C[1, 0]), 72.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 116.0);
}

TEST(LinalgLevel3RankUpdate, HermitianRankKOverwriteAndUpdate) {
    complex a_data[] = {{1.0, 1.0}, {2.0, 0.0}, {3.0, 0.0}, {4.0, -1.0}};
    complex e_data[] = {{10.0, 9.0}, {10.0, 0.0}, {10.0, 0.0}, {10.0, 9.0}};
    complex c_data[] = {{99.0, 0.0}, {99.0, 0.0}, {99.0, 0.0}, {99.0, 0.0}};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan C(c_data, std::extents<int, 2, 2>{});

    std::linalg::hermitian_matrix_rank_k_update(
        complex{2.0, 0.0}, A, C, std::linalg::upper_triangle);
    expect_complex_near(C[0, 0], {12.0, 0.0});
    expect_complex_near(C[0, 1], {22.0, 10.0});
    expect_complex_near(C[1, 0], {99.0, 0.0});

    std::linalg::hermitian_matrix_rank_k_update(
        complex{2.0, 0.0}, A, E, C, std::linalg::lower_triangle);
    expect_complex_near(C[1, 0], {32.0, -10.0});
    expect_complex_near(C[1, 1], {62.0, 0.0});
}

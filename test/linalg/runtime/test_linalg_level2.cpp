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

template<class Vec>
void expect_vector_near(Vec v, std::initializer_list<double> expected)
{
    int i = 0;
    for (double value : expected) {
        EXPECT_NEAR(v[i], value, 1e-12);
        ++i;
    }
}

} // namespace

TEST(LinalgLevel2Gemv, RectangularAndUpdateForms) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {10.0, 20.0};
    double z_data[] = {0.0, 0.0};
    std::mdspan A(a_data, std::extents<int, 2, 3>{});
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::mdspan z(z_data, std::extents<int, 2>{});

    std::linalg::matrix_vector_product(A, x, z);
    expect_vector_near(z, {14.0, 32.0});

    std::linalg::matrix_vector_product(A, x, y, z);
    expect_vector_near(z, {24.0, 52.0});
}

TEST(LinalgLevel2Gemv, LayoutLeftAndLayoutStrideInputs) {
    double left_data[6]{};
    std::mdspan<double, std::extents<int, 2, 3>, std::layout_left> left(left_data);
    left[0, 0] = 1.0; left[0, 1] = 2.0; left[0, 2] = 3.0;
    left[1, 0] = 4.0; left[1, 1] = 5.0; left[1, 2] = 6.0;

    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {0.0, 0.0};
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::linalg::matrix_vector_product(left, x, y);
    expect_vector_near(y, {14.0, 32.0});

    double stride_data[] = {1.0, 2.0, 3.0, -100.0, 4.0, 5.0, 6.0};
    using extents_t = std::extents<int, 2, 3>;
    using mapping_t = std::layout_stride::mapping<extents_t>;
    mapping_t mapping(extents_t{}, std::array<int, 2>{4, 1});
    std::mdspan<double, extents_t, std::layout_stride> stride(stride_data, mapping);
    std::linalg::matrix_vector_product(stride, x, y);
    expect_vector_near(y, {14.0, 32.0});
}

TEST(LinalgLevel2Gemv, MixedElementTypesUseScalarFallback) {
    float a_data[] = {1.25f, 2.5f, 3.75f, 4.5f};
    double x_data[] = {2.0, -1.0};
    double y_data[] = {0.0, 0.0};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    std::linalg::matrix_vector_product(A, x, y);

    expect_vector_near(y, {0.0, 3.0});
}

TEST(LinalgLevel2Gemv, ScaledAndTransposedViews) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0};
    double x_data[] = {1.0, 2.0};
    double y_data[] = {0.0, 0.0};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    std::linalg::matrix_vector_product(std::linalg::scaled(2.0, A), x, y);
    expect_vector_near(y, {10.0, 22.0});

    std::linalg::matrix_vector_product(std::linalg::transposed(A), x, y);
    expect_vector_near(y, {7.0, 10.0});
}

TEST(LinalgLevel2TriangularProduct, UpperLowerExplicitAndUnitDiagonal) {
    double upper_data[] = {1.0, 2.0, 3.0, 100.0, 4.0, 5.0, 100.0, 100.0, 6.0};
    double lower_data[] = {1.0, 100.0, 100.0, 2.0, 3.0, 100.0, 4.0, 5.0, 6.0};
    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {0.0, 0.0, 0.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 3>{});

    std::linalg::triangular_matrix_vector_product(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x, y);
    expect_vector_near(y, {14.0, 23.0, 18.0});

    std::linalg::triangular_matrix_vector_product(
        lower, std::linalg::lower_triangle, std::linalg::explicit_diagonal, x, y);
    expect_vector_near(y, {1.0, 8.0, 32.0});

    double unit_data[] = {99.0, 2.0, 3.0, 100.0, 99.0, 5.0, 100.0, 100.0, 99.0};
    std::mdspan unit(unit_data, std::extents<int, 3, 3>{});
    std::linalg::triangular_matrix_vector_product(
        unit, std::linalg::upper_triangle, std::linalg::implicit_unit_diagonal, x, y);
    expect_vector_near(y, {14.0, 17.0, 3.0});
}

TEST(LinalgLevel2TriangularProduct, LowerInPlaceKeepsOriginalVectorInputs) {
    double lower_data[] = {1.0, 100.0, 100.0, 2.0, 3.0, 100.0, 4.0, 5.0, 6.0};
    double x_data[] = {1.0, 2.0, 3.0};
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan x(x_data, std::extents<int, 3>{});

    std::linalg::triangular_matrix_vector_product(
        lower, std::linalg::lower_triangle, std::linalg::explicit_diagonal, x);
    expect_vector_near(x, {1.0, 8.0, 32.0});
}

TEST(LinalgLevel2TriangularSolve, UpperLowerAndImplicitUnitDiagonal) {
    double upper_data[] = {2.0, 1.0, -1.0, 100.0, 3.0, 2.0, 100.0, 100.0, 4.0};
    double upper_rhs[] = {1.0, 12.0, 12.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan upper_x(upper_rhs, std::extents<int, 3>{});
    std::linalg::triangular_matrix_vector_solve(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal, upper_x);
    expect_vector_near(upper_x, {1.0, 2.0, 3.0});

    double lower_data[] = {2.0, 100.0, 100.0, 3.0, 1.0, 100.0, 1.0, -2.0, 4.0};
    double lower_rhs[] = {2.0, 5.0, 9.0};
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan lower_x(lower_rhs, std::extents<int, 3>{});
    std::linalg::triangular_matrix_vector_solve(
        lower, std::linalg::lower_triangle, std::linalg::explicit_diagonal, lower_x);
    expect_vector_near(lower_x, {1.0, 2.0, 3.0});

    double unit_lower_data[] = {99.0, 100.0, 100.0, 3.0, 99.0, 100.0, 1.0, -2.0, 99.0};
    double unit_rhs[] = {1.0, 5.0, 0.0};
    std::mdspan unit_lower(unit_lower_data, std::extents<int, 3, 3>{});
    std::mdspan unit_x(unit_rhs, std::extents<int, 3>{});
    std::linalg::triangular_matrix_vector_solve(
        unit_lower, std::linalg::lower_triangle, std::linalg::implicit_unit_diagonal, unit_x);
    expect_vector_near(unit_x, {1.0, 2.0, 3.0});
}

TEST(LinalgLevel2SymmetricProduct, UpperLowerAndUpdateForms) {
    double upper_data[] = {1.0, 2.0, 3.0, 100.0, 4.0, 5.0, 100.0, 100.0, 6.0};
    double lower_data[] = {1.0, 100.0, 100.0, 2.0, 4.0, 100.0, 3.0, 5.0, 6.0};
    double x_data[] = {1.0, 2.0, 3.0};
    double base_data[] = {10.0, 20.0, 30.0};
    double y_data[] = {0.0, 0.0, 0.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan lower(lower_data, std::extents<int, 3, 3>{});
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan base(base_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 3>{});

    std::linalg::symmetric_matrix_vector_product(upper, std::linalg::upper_triangle, x, y);
    expect_vector_near(y, {14.0, 25.0, 31.0});

    std::linalg::symmetric_matrix_vector_product(lower, std::linalg::lower_triangle, x, y);
    expect_vector_near(y, {14.0, 25.0, 31.0});

    std::linalg::symmetric_matrix_vector_product(upper, std::linalg::upper_triangle, x, base, y);
    expect_vector_near(y, {24.0, 45.0, 61.0});
}

TEST(LinalgLevel2SymmetricProduct, UpdateOutputMayAliasInput) {
    double a_data[] = {1.0, 2.0, 100.0, 3.0};
    double x_data[] = {1.0, 1.0};
    double y_data[] = {10.0, 20.0};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    std::linalg::symmetric_matrix_vector_product(
        A, std::linalg::upper_triangle, x, y, y);

    expect_vector_near(y, {13.0, 25.0});
}

TEST(LinalgLevel2HermitianProduct, IgnoresDiagonalImaginaryAndSupportsLower) {
    complex upper_data[] = {{2.0, 99.0}, {1.0, 1.0}, {99.0, 99.0}, {3.0, -88.0}};
    complex lower_data[] = {{2.0, 99.0}, {99.0, 99.0}, {1.0, -1.0}, {3.0, -88.0}};
    complex x_data[] = {{1.0, 1.0}, {2.0, -1.0}};
    complex base_data[] = {{10.0, 0.0}, {20.0, 0.0}};
    complex y_data[] = {{0.0, 0.0}, {0.0, 0.0}};
    std::mdspan upper(upper_data, std::extents<int, 2, 2>{});
    std::mdspan lower(lower_data, std::extents<int, 2, 2>{});
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan base(base_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    std::linalg::hermitian_matrix_vector_product(upper, std::linalg::upper_triangle, x, y);
    expect_complex_near(y[0], {5.0, 3.0});
    expect_complex_near(y[1], {8.0, -3.0});

    std::linalg::hermitian_matrix_vector_product(lower, std::linalg::lower_triangle, x, y);
    expect_complex_near(y[0], {5.0, 3.0});
    expect_complex_near(y[1], {8.0, -3.0});

    std::linalg::hermitian_matrix_vector_product(upper, std::linalg::upper_triangle, x, base, y);
    expect_complex_near(y[0], {15.0, 3.0});
    expect_complex_near(y[1], {28.0, -3.0});
}

TEST(LinalgLevel2HermitianProduct, UpdateOutputMayAliasInput) {
    complex a_data[] = {{2.0, 9.0}, {1.0, 1.0}, {100.0, 100.0}, {3.0, -9.0}};
    complex x_data[] = {{1.0, 0.0}, {2.0, 0.0}};
    complex y_data[] = {{10.0, 0.0}, {20.0, 0.0}};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    std::linalg::hermitian_matrix_vector_product(
        A, std::linalg::upper_triangle, x, y, y);

    expect_complex_near(y[0], {14.0, 2.0});
    expect_complex_near(y[1], {27.0, -1.0});
}

TEST(LinalgLevel2RankUpdate, NonsymmetricOverwriteAndUpdateForms) {
    double x_data[] = {1.0, 2.0};
    double y_data[] = {3.0, 4.0, 5.0};
    double e_data[] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
    double a_data[] = {99.0, 99.0, 99.0, 99.0, 99.0, 99.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 3>{});
    std::mdspan E(e_data, std::extents<int, 2, 3>{});
    std::mdspan A(a_data, std::extents<int, 2, 3>{});

    std::linalg::matrix_rank_1_update(x, y, A);
    EXPECT_DOUBLE_EQ((A[0, 0]), 3.0);
    EXPECT_DOUBLE_EQ((A[0, 2]), 5.0);
    EXPECT_DOUBLE_EQ((A[1, 2]), 10.0);

    std::linalg::matrix_rank_1_update(x, y, E, A);
    EXPECT_DOUBLE_EQ((A[0, 0]), 13.0);
    EXPECT_DOUBLE_EQ((A[1, 2]), 20.0);
}

TEST(LinalgLevel2RankUpdate, ConjugatedNonsymmetricForms) {
    complex x_data[] = {{1.0, 1.0}};
    complex y_data[] = {{2.0, 3.0}, {4.0, -1.0}};
    complex e_data[] = {{10.0, 0.0}, {10.0, 0.0}};
    complex a_data[] = {{0.0, 0.0}, {0.0, 0.0}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::mdspan E(e_data, std::extents<int, 1, 2>{});
    std::mdspan A(a_data, std::extents<int, 1, 2>{});

    std::linalg::matrix_rank_1_update_c(x, y, A);
    expect_complex_near(A[0, 0], {5.0, -1.0});
    expect_complex_near(A[0, 1], {3.0, 5.0});

    std::linalg::matrix_rank_1_update_c(x, y, E, A);
    expect_complex_near(A[0, 0], {15.0, -1.0});
    expect_complex_near(A[0, 1], {13.0, 5.0});
}

TEST(LinalgLevel2RankUpdate, SymmetricRankUpdatesRespectTriangle) {
    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {3.0, 4.0, 5.0};
    double e_data[] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
    double a_data[] = {99.0, 99.0, 99.0, 99.0, 99.0, 99.0, 99.0, 99.0, 99.0};
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 3>{});
    std::mdspan E(e_data, std::extents<int, 3, 3>{});
    std::mdspan A(a_data, std::extents<int, 3, 3>{});

    std::linalg::symmetric_matrix_rank_1_update(2.0, x, A, std::linalg::upper_triangle);
    EXPECT_DOUBLE_EQ((A[0, 0]), 2.0);
    EXPECT_DOUBLE_EQ((A[0, 2]), 6.0);
    EXPECT_DOUBLE_EQ((A[2, 0]), 99.0);

    std::linalg::symmetric_matrix_rank_1_update(2.0, x, E, A, std::linalg::lower_triangle);
    EXPECT_DOUBLE_EQ((A[2, 0]), 16.0);
    EXPECT_DOUBLE_EQ((A[0, 2]), 6.0);

    std::linalg::symmetric_matrix_rank_2_update(x, y, A, std::linalg::upper_triangle);
    EXPECT_DOUBLE_EQ((A[0, 0]), 6.0);
    EXPECT_DOUBLE_EQ((A[0, 1]), 10.0);
    EXPECT_DOUBLE_EQ((A[1, 1]), 16.0);

    std::linalg::symmetric_matrix_rank_2_update(x, y, E, A, std::linalg::lower_triangle);
    EXPECT_DOUBLE_EQ((A[1, 0]), 20.0);
    EXPECT_DOUBLE_EQ((A[2, 2]), 40.0);
}

TEST(LinalgLevel2RankUpdate, HermitianRankUpdatesRespectTriangle) {
    complex x_data[] = {{1.0, 1.0}, {2.0, 0.0}};
    complex y_data[] = {{3.0, 0.0}, {4.0, -1.0}};
    complex e_data[] = {{10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}};
    complex a_data[] = {{99.0, 0.0}, {99.0, 0.0}, {99.0, 0.0}, {99.0, 0.0}};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan A(a_data, std::extents<int, 2, 2>{});

    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 0.0}, x, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {4.0, 0.0});
    expect_complex_near(A[0, 1], {4.0, 4.0});
    expect_complex_near(A[1, 0], {99.0, 0.0});

    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 0.0}, x, E, A, std::linalg::lower_triangle);
    expect_complex_near(A[1, 0], {14.0, -4.0});

    std::linalg::hermitian_matrix_rank_2_update(x, y, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {6.0, 0.0});
    expect_complex_near(A[0, 1], {9.0, 5.0});
    expect_complex_near(A[1, 1], {16.0, 0.0});

    std::linalg::hermitian_matrix_rank_2_update(x, y, E, A, std::linalg::lower_triangle);
    expect_complex_near(A[1, 0], {19.0, -5.0});
}

TEST(LinalgLevel2RankUpdate, HermitianRankUpdatesKeepDiagonalReal) {
    complex x_data[] = {{1.0, 1.0}};
    complex y_data[] = {{2.0, 3.0}};
    complex e_data[] = {{10.0, 9.0}};
    complex a_data[] = {{0.0, 0.0}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    std::mdspan E(e_data, std::extents<int, 1, 1>{});
    std::mdspan A(a_data, std::extents<int, 1, 1>{});

    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 3.0}, x, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {4.0, 0.0});

    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 3.0}, x, E, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {14.0, 0.0});

    std::linalg::hermitian_matrix_rank_2_update(x, y, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {10.0, 0.0});

    std::linalg::hermitian_matrix_rank_2_update(x, y, E, A, std::linalg::upper_triangle);
    expect_complex_near(A[0, 0], {20.0, 0.0});
}

TEST(LinalgLevel2RankUpdate, HermitianRankOneUsesRealPartOfComplexAlpha) {
    complex x_data[] = {{1.0, 1.0}, {2.0, 0.0}};
    complex e_data[] = {{10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}};
    complex upper_data[4]{};
    complex lower_data[4]{};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan upper(upper_data, std::extents<int, 2, 2>{});
    std::mdspan lower(lower_data, std::extents<int, 2, 2>{});

    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 3.0}, x, upper, std::linalg::upper_triangle);
    std::linalg::hermitian_matrix_rank_1_update(
        complex{2.0, 3.0}, x, E, lower, std::linalg::lower_triangle);

    expect_complex_near(upper[0, 1], {4.0, 4.0});
    expect_complex_near(lower[1, 0], {14.0, -4.0});
    expect_complex_near(upper[0, 1], std::conj(lower[1, 0] - complex{10.0, 0.0}));
}

#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <complex>
#include <limits>
#include <stdexcept>

namespace {

using complex = std::complex<double>;

template<class T>
struct writable_const_accessor {
    using offset_policy = writable_const_accessor;
    using element_type = const T;
    using reference = T&;
    using data_handle_type = T*;

    [[nodiscard]] constexpr reference
    access(data_handle_type ptr, std::size_t index) const noexcept {
        return ptr[index];
    }

    [[nodiscard]] constexpr data_handle_type
    offset(data_handle_type ptr, std::size_t index) const noexcept {
        return ptr + index;
    }
};

void expect_complex_near(complex actual, complex expected)
{
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

} // namespace

static_assert(
    std::linalg::__detail::__checked_matrix_storage_size(50'000, 50'000) ==
    std::size_t{2'500'000'000});

TEST(LinalgLevel3Storage, RejectsUnrepresentableTemporarySize) {
    EXPECT_THROW(
        (void)std::linalg::__detail::__checked_matrix_storage_size(
            std::numeric_limits<std::size_t>::max(), std::size_t{2}),
        std::length_error);
}

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

TEST(LinalgLevel3Gemm, InnerExtentDoesNotNarrowToOutputIndex) {
    std::array<double, 256> a_data;
    std::array<double, 256> b_data;
    a_data.fill(1.0);
    b_data.fill(1.0);
    double output = -1.0;
    double control = -1.0;
    double input = 5.0;
    std::mdspan a(a_data.data(), std::extents<int, 1, 256>{});
    std::mdspan b(b_data.data(), std::extents<int, 256, 1>{});
    std::mdspan c(&output, std::extents<std::uint8_t, 1, 1>{});
    std::mdspan wide(&control, std::extents<int, 1, 1>{});
    std::mdspan e(&input, std::extents<int, 1, 1>{});
    std::linalg::matrix_product(a, b, wide);
    EXPECT_DOUBLE_EQ(control, 256.0);
    std::linalg::matrix_product(a, b, c);
    EXPECT_DOUBLE_EQ(output, control);
    std::linalg::matrix_product(a, b, e, c);
    EXPECT_DOUBLE_EQ(output, 261.0);
}

TEST(LinalgLevel3Gemm, WritableConstElementAccessorsUseMutableAccumulators) {
    using vector_extents = std::extents<int, 1>;
    using matrix_extents = std::extents<int, 1, 1>;
    using accessor = writable_const_accessor<double>;

    double a_data[] = {2.0};
    double b_data[] = {4.0};
    double x_data[] = {3.0};
    double y_data[] = {0.0};
    double c_data[] = {0.0};
    std::mdspan A(a_data, matrix_extents{});
    std::mdspan B(b_data, matrix_extents{});
    std::mdspan x(x_data, vector_extents{});
    std::mdspan<const double, vector_extents, std::layout_right, accessor>
        y(y_data);
    std::mdspan<const double, matrix_extents, std::layout_right, accessor>
        C(c_data);

    std::linalg::matrix_vector_product(A, x, y);
    std::linalg::matrix_product(A, B, C);

    EXPECT_DOUBLE_EQ(y_data[0], 6.0);
    EXPECT_DOUBLE_EQ(c_data[0], 8.0);
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

    std::copy(std::begin(b_data), std::end(b_data), std::begin(c_data));
    std::linalg::triangular_matrix_left_product(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 37.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 36.0);

    std::copy(std::begin(b_data), std::end(b_data), std::begin(c_data));
    std::linalg::triangular_matrix_left_product(
        lower, std::linalg::lower_triangle, std::linalg::explicit_diagonal, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 1.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 16.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 64.0);

    double unit_data[] = {99.0, 2.0, 3.0, 100.0, 99.0, 5.0, 100.0, 100.0, 99.0};
    std::mdspan unit(unit_data, std::extents<int, 3, 3>{});
    std::copy(std::begin(b_data), std::end(b_data), std::begin(c_data));
    std::linalg::triangular_matrix_left_product(
        unit, std::linalg::upper_triangle, std::linalg::implicit_unit_diagonal, C);
    EXPECT_DOUBLE_EQ((C[0, 0]), 22.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 28.0);
    EXPECT_DOUBLE_EQ((C[2, 1]), 6.0);
}

TEST(LinalgLevel3TriangularProduct, RightProductUsesTriangularMatrixOnRight) {
    double upper_data[] = {1.0, 2.0, 3.0, 100.0, 4.0, 5.0, 100.0, 100.0, 6.0};
    double c_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::mdspan upper(upper_data, std::extents<int, 3, 3>{});
    std::mdspan C(c_data, std::extents<int, 2, 3>{});

    std::linalg::triangular_matrix_right_product(
        upper, std::linalg::upper_triangle, std::linalg::explicit_diagonal, C);

    EXPECT_DOUBLE_EQ((C[0, 0]), 1.0);
    EXPECT_DOUBLE_EQ((C[0, 1]), 10.0);
    EXPECT_DOUBLE_EQ((C[0, 2]), 31.0);
    EXPECT_DOUBLE_EQ((C[1, 0]), 4.0);
    EXPECT_DOUBLE_EQ((C[1, 1]), 28.0);
    EXPECT_DOUBLE_EQ((C[1, 2]), 73.0);
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

TEST(LinalgLevel3RankUpdate, HermitianRankKUsesRealPartOfComplexAlpha) {
    complex a_data[] = {{1.0, 1.0}, {2.0, 0.0}, {3.0, 0.0}, {4.0, -1.0}};
    complex e_data[] = {{10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}, {10.0, 0.0}};
    complex upper_data[4]{};
    complex lower_data[4]{};
    std::mdspan A(a_data, std::extents<int, 2, 2>{});
    std::mdspan E(e_data, std::extents<int, 2, 2>{});
    std::mdspan upper(upper_data, std::extents<int, 2, 2>{});
    std::mdspan lower(lower_data, std::extents<int, 2, 2>{});

    std::linalg::hermitian_matrix_rank_k_update(
        complex{2.0, 3.0}, A, upper, std::linalg::upper_triangle);
    std::linalg::hermitian_matrix_rank_k_update(
        complex{2.0, 3.0}, A, E, lower, std::linalg::lower_triangle);

    expect_complex_near(upper[0, 1], {22.0, 10.0});
    expect_complex_near(lower[1, 0], {32.0, -10.0});
    expect_complex_near(upper[0, 1], std::conj(lower[1, 0] - complex{10.0, 0.0}));
}

TEST(LinalgLevel3RankUpdate, SymmetricRankKInnerExtentDoesNotNarrowToOutputIndex) {
    for (int k : {0, 3, 255, 256, 257}) {
        SCOPED_TRACE(k);
        std::array<double, 2 * 257> a_data{};
        std::mdspan A(a_data.data(), std::extents<int, 2, std::dynamic_extent>{k});
        for (int l = 0; l < k; ++l) {
            A[0, l] = 1.0;
            A[1, l] = 2.0;
        }
        const std::array<double, 4> initial = {5.0, 7.0, 7.0, 13.0};
        std::array<double, 4> e_data = initial;
        std::array<double, 4> c_data;
        std::mdspan E(e_data.data(), std::extents<int, 2, 2>{});
        std::mdspan C(c_data.data(), std::extents<std::uint8_t, 2, 2>{});

        auto check_triangle = [&](auto triangle, bool upper) {
            SCOPED_TRACE(upper ? "upper" : "lower");
            auto expect_result = [&](bool update, bool alias) {
                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 2; ++j) {
                        const int offset = 2 * i + j;
                        const bool selected = upper ? i <= j : j <= i;
                        const double product = 2.0 * k * (i + 1) * (j + 1);
                        const double expected = selected
                            ? product + (update ? initial[offset] : 0.0)
                            : (alias ? initial[offset] : -99.0);
                        EXPECT_DOUBLE_EQ((C[i, j]), expected);
                    }
                }
            };
            c_data.fill(-99.0);
            std::linalg::symmetric_matrix_rank_k_update(2.0, A, C, triangle);
            expect_result(false, false);
            for (bool alias : {false, true}) {
                SCOPED_TRACE(alias ? "E==C" : "separate E/C");
                c_data.fill(-99.0);
                if (alias) c_data = initial;
                if (alias)
                    std::linalg::symmetric_matrix_rank_k_update(2.0, A, C, C, triangle);
                else
                    std::linalg::symmetric_matrix_rank_k_update(2.0, A, E, C, triangle);
                expect_result(true, alias);
                EXPECT_EQ(e_data, initial);
            }
        };
        check_triangle(std::linalg::upper_triangle, true);
        check_triangle(std::linalg::lower_triangle, false);
    }
}

TEST(LinalgLevel3RankUpdate, SymmetricRank2KInnerExtentDoesNotNarrowToOutputIndex) {
    for (int k : {0, 3, 255, 256, 257}) {
        SCOPED_TRACE(k);
        std::array<double, 2 * 257> a_data{};
        std::array<double, 2 * 257> b_data{};
        std::mdspan A(a_data.data(), std::extents<int, 2, std::dynamic_extent>{k});
        std::mdspan B(b_data.data(), std::extents<int, 2, std::dynamic_extent>{k});
        for (int l = 0; l < k; ++l) {
            A[0, l] = 1.0;
            A[1, l] = 2.0;
            B[0, l] = 3.0;
            B[1, l] = 4.0;
        }
        const std::array<double, 4> initial = {5.0, 7.0, 7.0, 13.0};
        std::array<double, 4> e_data = initial;
        std::array<double, 4> c_data;
        std::mdspan E(e_data.data(), std::extents<int, 2, 2>{});
        std::mdspan C(c_data.data(), std::extents<std::uint8_t, 2, 2>{});

        auto check_triangle = [&](auto triangle, bool upper) {
            SCOPED_TRACE(upper ? "upper" : "lower");
            auto expect_result = [&](double scale, bool update, bool alias) {
                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 2; ++j) {
                        const int offset = 2 * i + j;
                        const bool selected = upper ? i <= j : j <= i;
                        const double product = scale * k *
                            ((i + 1) * (j + 3) + (i + 3) * (j + 1));
                        const double expected = selected
                            ? product + (update ? initial[offset] : 0.0)
                            : (alias ? initial[offset] : -99.0);
                        EXPECT_DOUBLE_EQ((C[i, j]), expected);
                    }
                }
            };
            c_data.fill(-99.0);
            std::linalg::symmetric_matrix_rank_2k_update(A, B, C, triangle);
            expect_result(1.0, false, false);
            c_data.fill(-99.0);
            std::linalg::symmetric_matrix_rank_2k_update(2.0, triangle, A, B, C);
            expect_result(2.0, false, false);
            for (bool alias : {false, true}) {
                SCOPED_TRACE(alias ? "E==C" : "separate E/C");
                c_data.fill(-99.0);
                if (alias) c_data = initial;
                if (alias)
                    std::linalg::symmetric_matrix_rank_2k_update(A, B, C, C, triangle);
                else
                    std::linalg::symmetric_matrix_rank_2k_update(A, B, E, C, triangle);
                expect_result(1.0, true, alias);
                EXPECT_EQ(e_data, initial);
            }
        };
        check_triangle(std::linalg::upper_triangle, true);
        check_triangle(std::linalg::lower_triangle, false);
    }
}

TEST(LinalgLevel3RankUpdate, HermitianRankKInnerExtentDoesNotNarrowToOutputIndex) {
    for (int k : {0, 3, 255, 256, 257}) {
        SCOPED_TRACE(k);
        std::array<complex, 2 * 257> a_data{};
        std::mdspan A(a_data.data(), std::extents<int, 2, std::dynamic_extent>{k});
        for (int l = 0; l < k; ++l) {
            A[0, l] = {1.0, 1.0};
            A[1, l] = {2.0, -1.0};
        }
        const std::array<complex, 4> initial = {
            complex{5.0, 0.0}, complex{7.0, 2.0}, complex{7.0, -2.0}, complex{13.0, 0.0}};
        const std::array<complex, 4> products = {
            complex{2.0, 0.0}, complex{1.0, 3.0}, complex{1.0, -3.0}, complex{5.0, 0.0}};
        std::array<complex, 4> e_data = initial;
        std::array<complex, 4> c_data;
        std::mdspan E(e_data.data(), std::extents<int, 2, 2>{});
        std::mdspan C(c_data.data(), std::extents<std::uint8_t, 2, 2>{});

        auto check_triangle = [&](auto triangle, bool upper) {
            SCOPED_TRACE(upper ? "upper" : "lower");
            auto expect_result = [&](bool update, bool alias) {
                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 2; ++j) {
                        const int offset = 2 * i + j;
                        const bool selected = upper ? i <= j : j <= i;
                        const complex expected = selected
                            ? 2.0 * k * products[offset] + (update ? initial[offset] : complex{})
                            : (alias ? initial[offset] : complex{-99.0, -99.0});
                        expect_complex_near(C[i, j], expected);
                    }
                }
            };
            c_data.fill(complex{-99.0, -99.0});
            std::linalg::hermitian_matrix_rank_k_update(2.0, A, C, triangle);
            expect_result(false, false);
            for (bool alias : {false, true}) {
                SCOPED_TRACE(alias ? "E==C" : "separate E/C");
                c_data.fill(complex{-99.0, -99.0});
                if (alias) c_data = initial;
                if (alias)
                    std::linalg::hermitian_matrix_rank_k_update(2.0, A, C, C, triangle);
                else
                    std::linalg::hermitian_matrix_rank_k_update(2.0, A, E, C, triangle);
                expect_result(true, alias);
                EXPECT_EQ(e_data, initial);
            }
        };
        check_triangle(std::linalg::upper_triangle, true);
        check_triangle(std::linalg::lower_triangle, false);
    }
}

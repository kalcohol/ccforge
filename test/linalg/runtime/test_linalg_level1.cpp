#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <type_traits>

TEST(LinalgLevel1CopyScaleSwapAdd, DenseVectors) {
    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {4.0, 5.0, 6.0};
    double z_data[] = {0.0, 0.0, 0.0};
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 3>{});
    std::mdspan z(z_data, std::extents<int, 3>{});

    std::linalg::copy(x, z);
    EXPECT_DOUBLE_EQ(z[0], 1.0);
    EXPECT_DOUBLE_EQ(z[1], 2.0);
    EXPECT_DOUBLE_EQ(z[2], 3.0);

    std::linalg::scale(2.0, z);
    EXPECT_DOUBLE_EQ(z[0], 2.0);
    EXPECT_DOUBLE_EQ(z[1], 4.0);
    EXPECT_DOUBLE_EQ(z[2], 6.0);

    std::linalg::swap_elements(x, y);
    EXPECT_DOUBLE_EQ(x[0], 4.0);
    EXPECT_DOUBLE_EQ(y[0], 1.0);

    std::linalg::add(x, y, z);
    EXPECT_DOUBLE_EQ(z[0], 5.0);
    EXPECT_DOUBLE_EQ(z[1], 7.0);
    EXPECT_DOUBLE_EQ(z[2], 9.0);
}

TEST(LinalgLevel1CopyScaleSwapAdd, DenseMatrices) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0};
    double b_data[] = {5.0, 6.0, 7.0, 8.0};
    double c_data[] = {0.0, 0.0, 0.0, 0.0};
    std::mdspan a(a_data, std::extents<int, 2, 2>{});
    std::mdspan b(b_data, std::extents<int, 2, 2>{});
    std::mdspan c(c_data, std::extents<int, 2, 2>{});

    std::linalg::copy(a, c);
    EXPECT_DOUBLE_EQ((c[1, 1]), 4.0);

    std::linalg::scale(3.0, c);
    EXPECT_DOUBLE_EQ((c[0, 1]), 6.0);
    EXPECT_DOUBLE_EQ((c[1, 1]), 12.0);

    std::linalg::swap_elements(a, b);
    EXPECT_DOUBLE_EQ((a[0, 0]), 5.0);
    EXPECT_DOUBLE_EQ((b[1, 1]), 4.0);

    std::linalg::add(a, b, c);
    EXPECT_DOUBLE_EQ((c[0, 0]), 6.0);
    EXPECT_DOUBLE_EQ((c[1, 1]), 12.0);
}

TEST(LinalgLevel1CompatibleExtents, AcceptsDifferentMdspanExtentsTypes) {
    std::array<double, 3> x_data{1.0, 2.0, 3.0};
    std::array<double, 3> y_data{4.0, 5.0, 6.0};
    std::array<double, 3> z_data{};

    std::mdspan<double, std::extents<std::size_t, 3>> x(x_data.data());
    std::mdspan<double, std::dextents<int, 1>> y(y_data.data(), 3);
    std::mdspan<double, std::extents<long, std::dynamic_extent>> z(
        z_data.data(), 3);

    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y, 0.0), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dotc(x, y, 0.0), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dotc(x, y), 32.0);

    std::linalg::add(x, y, z);
    EXPECT_EQ(z_data, (std::array<double, 3>{5.0, 7.0, 9.0}));

    std::linalg::swap_elements(x, y);
    EXPECT_EQ(x_data, (std::array<double, 3>{4.0, 5.0, 6.0}));
    EXPECT_EQ(y_data, (std::array<double, 3>{1.0, 2.0, 3.0}));

    std::linalg::apply_givens_rotation(x, y, 0.0, 1.0);
    EXPECT_EQ(x_data, (std::array<double, 3>{1.0, 2.0, 3.0}));
    EXPECT_EQ(y_data, (std::array<double, 3>{-4.0, -5.0, -6.0}));
}

TEST(LinalgLevel1Dot, LayoutStrideUsesMappedElements) {
    double x_data[] = {1.0, 100.0, 2.0, 100.0, 3.0};
    double y_data[] = {4.0, 100.0, 5.0, 100.0, 6.0};
    using extents_t = std::dextents<int, 1>;
    using mapping_t = std::layout_stride::mapping<extents_t>;
    mapping_t mapping(extents_t{3}, std::array<int, 1>{2});
    std::mdspan<double, extents_t, std::layout_stride> x(x_data, mapping);
    std::mdspan<double, extents_t, std::layout_stride> y(y_data, mapping);

    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y), 32.0);
}

TEST(LinalgLevel1DotSIMD, DoubleEquivalence) {
    constexpr int N = 1024;
    static double x[N], y[N];
    for (int i = 0; i < N; ++i) { x[i] = static_cast<double>(i+1); y[i] = 1.0/(i+1); }
    std::mdspan xv(x, std::extents<int, N>{});
    std::mdspan yv(y, std::extents<int, N>{});
    double result = std::linalg::dot(xv, yv, 0.0);
    EXPECT_NEAR(result, static_cast<double>(N), 1e-10);
}

TEST(LinalgLevel1Dot, WiderInitAccumulatesProductsInInitType) {
    float x_data[] = {1.0e20f};
    float y_data[] = {1.0e20f};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});

    const double result = std::linalg::dot(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 1.0e40, 1.0e34);
}

TEST(LinalgLevel1Dotc, RealAndComplexInputs) {
    double real_x_data[] = {1.0, 2.0, 3.0};
    double real_y_data[] = {4.0, 5.0, 6.0};
    std::mdspan real_x(real_x_data, std::extents<int, 3>{});
    std::mdspan real_y(real_y_data, std::extents<int, 3>{});
    EXPECT_DOUBLE_EQ(std::linalg::dotc(real_x, real_y), 32.0);

    using complex = std::complex<double>;
    complex complex_x_data[] = {{1.0, 1.0}, {2.0, -1.0}};
    complex complex_y_data[] = {{3.0, 2.0}, {4.0, -1.0}};
    std::mdspan complex_x(complex_x_data, std::extents<int, 2>{});
    std::mdspan complex_y(complex_y_data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::dotc(complex_x, complex_y), complex(14.0, 1.0));
}

TEST(LinalgLevel1Dotc, WiderInitAccumulatesProductsInInitType) {
    float x_data[] = {1.0e20f};
    float y_data[] = {1.0e20f};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});

    const double result = std::linalg::dotc(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 1.0e40, 1.0e34);
}

TEST(LinalgLevel1Reductions, ExplicitInitControlsReturnType) {
    double x_data[] = {3.0, 4.0};
    double y_data[] = {5.0, 6.0};
    double matrix_data[] = {1.0, -2.0, 3.0, -4.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::vector_two_norm(x, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::vector_abs_sum(x, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_frob_norm(matrix, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_one_norm(matrix, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_inf_norm(matrix, 0.0)), double>);
}

TEST(LinalgLevel1Reductions, VectorTwoNormCombinesInitEuclideanly) {
    double x_data[] = {4.0};
    std::mdspan x(x_data, std::extents<int, 1>{});

    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x, 3.0), 5.0);
}

TEST(LinalgLevel1Reductions, VectorTwoNormAvoidsIntermediateOverflow) {
    double x_data[] = {1.0e200, 1.0e200};
    std::mdspan x(x_data, std::extents<int, 2>{});

    const double result = std::linalg::vector_two_norm(x);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, std::sqrt(2.0) * 1.0e200, 1.0e185);
}

// The scaled-sum recurrence divides magnitudes; integral element types used
// to truncate those ratios to zero and report two_norm({3,4}) == 4. The
// integral paths accumulate in double, so results are exact only up to
// double's 2^53 integer precision (see the bound test below).
TEST(LinalgLevel1Reductions, VectorTwoNormIntegerElementsUseDoubleAccumulation) {
    int x_data[] = {3, 4};
    std::mdspan x(x_data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::vector_two_norm(x, 0), 5);
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x, 0.0), 5.0);
}

TEST(LinalgLevel1Reductions, MatrixFrobNormIntegerElementsUseDoubleAccumulation) {
    int a_data[] = {1, 2, 2, 4};
    std::mdspan a(a_data, std::extents<int, 2, 2>{});

    EXPECT_EQ(std::linalg::matrix_frob_norm(a, 0), 5);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(a, 0.0), 5.0);
}

// abs() of the most negative signed value is undefined; the magnitude
// helpers compute it in the unsigned counterpart instead.
TEST(LinalgLevel1Reductions, SignedMinimumMagnitudeIsWellDefined) {
    constexpr int int_min = std::numeric_limits<int>::min();
    int x_data[] = {int_min};
    std::mdspan x(x_data, std::extents<int, 1>{});

    // |INT_MIN| = 2^31 saturates an int-typed norm and stays exact in
    // wider result types.
    EXPECT_EQ(std::linalg::vector_two_norm(x, 0),
              std::numeric_limits<int>::max());
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x, 0.0), 2147483648.0);
    EXPECT_EQ(std::linalg::vector_abs_sum(x, std::int64_t{0}),
              std::int64_t{2147483648});

    int y_data[] = {5, int_min, 7};
    std::mdspan y(y_data, std::extents<int, 3>{});
    EXPECT_EQ(std::linalg::vector_idx_abs_max(y),
              decltype(y)::size_type{1});

    int matrix_data[] = {int_min, int_min, 1, 2};
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});
    static_assert(std::is_same_v<
                  decltype(std::linalg::matrix_one_norm(matrix)),
                  unsigned>);
    static_assert(std::is_same_v<
                  decltype(std::linalg::matrix_inf_norm(matrix)),
                  unsigned>);
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, std::int64_t{0}),
        std::int64_t{2147483650});
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, std::int64_t{0}),
        std::int64_t{4294967296});
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix),
        2147483650u);
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix),
        std::numeric_limits<unsigned>::max());
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, 0),
        std::numeric_limits<int>::max());
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, 0),
        std::numeric_limits<int>::max());
}

// Documents the double-precision boundary of the integral accumulation:
// past 2^53 the accumulator rounds, so results are "double-exact", not
// arbitrarily exact.
TEST(LinalgLevel1Reductions, IntegerNormsAreDoublePrecisionBounded) {
    constexpr std::int64_t big = std::int64_t{1} << 31;
    std::int64_t x_data[] = {big, 3};
    std::mdspan x(x_data, std::extents<int, 2>{});

    // big^2 + 9 = 2^62 + 9 rounds to 2^62 in double, so the small element
    // is absorbed and the norm comes back as exactly 2^31.
    EXPECT_EQ(std::linalg::vector_two_norm(x, std::int64_t{0}), big);

    // The frob-norm saturation branch mirrors vector_two_norm.
    std::int8_t a_data[] = {100, 100, 100, 100};
    std::mdspan a(a_data, std::extents<int, 2, 2>{});
    EXPECT_EQ(std::linalg::matrix_frob_norm(a, std::int8_t{0}),
              std::numeric_limits<std::int8_t>::max());

    // Unsigned 64-bit inputs saturate at the type maximum when the norm
    // exceeds it.
    constexpr std::uint64_t u_max = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t u_data[] = {u_max, u_max};
    std::mdspan u(u_data, std::extents<int, 2>{});
    EXPECT_EQ(std::linalg::vector_two_norm(u, std::uint64_t{0}), u_max);
}

// Same truncation family as two_norm: the integral scaled recurrence used to
// report {4, 1} for {3, 4}, reconstructing a norm of 4 instead of 5.
TEST(LinalgLevel1Reductions, VectorSumOfSquaresIntegerElementsAvoidTruncation) {
    int x_data[] = {3, 4};
    std::mdspan x(x_data, std::extents<int, 2>{});

    auto result = std::linalg::vector_sum_of_squares(
        x, std::linalg::sum_of_squares_result<int>{1, 0});
    EXPECT_EQ(result.scaling_factor, 1);
    EXPECT_EQ(result.scaled_sum_of_squares, 25);

    // A non-trivial init folds in as scaling_factor^2 * scaled_sum.
    auto combined = std::linalg::vector_sum_of_squares(
        x, std::linalg::sum_of_squares_result<int>{2, 3});
    EXPECT_EQ(combined.scaling_factor, 1);
    EXPECT_EQ(combined.scaled_sum_of_squares, 37);
}

// Casting an out-of-range double back to an integral norm type is UB; the
// backport saturates to the type's bounds instead.
TEST(LinalgLevel1Reductions, IntegerNormsOutOfRangeSaturate) {
    std::int8_t x_data[] = {100, 100, 100};
    std::mdspan x(x_data, std::extents<int, 3>{});

    EXPECT_EQ(
        std::linalg::vector_two_norm(x, std::int8_t{0}),
        std::numeric_limits<std::int8_t>::max());

    auto ssq = std::linalg::vector_sum_of_squares(
        x, std::linalg::sum_of_squares_result<std::int8_t>{1, 0});
    EXPECT_EQ(ssq.scaling_factor, 1);
    EXPECT_EQ(
        ssq.scaled_sum_of_squares,
        std::numeric_limits<std::int8_t>::max());
}

TEST(LinalgLevel1Reductions, ComplexInitUsesMagnitudeSquared) {
    using complex = std::complex<double>;
    double x_data[] = {4.0};
    std::mdspan x(x_data, std::extents<int, 1>{});
    EXPECT_EQ(
        std::linalg::vector_two_norm(x, complex{0.0, 3.0}),
        (complex{5.0, 0.0}));
}

TEST(LinalgLevel1Norm, DoubleEquivalence) {
    constexpr int N = 1024;
    static double x[N];
    for (int i = 0; i < N; ++i) x[i] = 1.0;
    std::mdspan xv(x, std::extents<int, N>{});
    double nrm = std::linalg::vector_two_norm(xv);
    EXPECT_NEAR(nrm, std::sqrt(static_cast<double>(N)), 1e-10);
}

TEST(LinalgLevel1AsumSIMD, DoubleEquivalence) {
    constexpr int N = 512;
    static double x[N];
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0 : -1.0;
    std::mdspan xv(x, std::extents<int, N>{});
    double asum = std::linalg::vector_abs_sum(xv);
    EXPECT_NEAR(asum, static_cast<double>(N), 1e-10);
}

TEST(LinalgLevel1AsumSIMD, FloatEquivalence) {
    constexpr int N = 512;
    static float x[N];
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    std::mdspan xv(x, std::extents<int, N>{});
    float asum = std::linalg::vector_abs_sum(xv);
    EXPECT_NEAR(asum, static_cast<float>(N), 1e-4f);
}

TEST(LinalgLevel1Asum, ComplexUsesOneNorm) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {-1.0, 2.0}};
    std::mdspan v(data, std::extents<int, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::vector_abs_sum(v)), double>);
    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(v), 10.0);
    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(v, 1.0), 11.0);
}

TEST(LinalgLevel1IdxAbsMax, HandlesEmptyTiesAndNegativeValues) {
    double empty_data[] = {0.0};
    std::mdspan empty(empty_data, std::extents<int, 0>{});
    using empty_view_t = decltype(empty);
    static_assert(std::is_same_v<
                  decltype(std::linalg::vector_idx_abs_max(empty)),
                  typename empty_view_t::size_type>);
    EXPECT_EQ(
        std::linalg::vector_idx_abs_max(empty),
        std::numeric_limits<typename empty_view_t::size_type>::max());

    double data[] = {-5.0, 2.0, 5.0, -4.0};
    std::mdspan v(data, std::extents<int, 4>{});
    EXPECT_EQ(
        std::linalg::vector_idx_abs_max(v),
        typename decltype(v)::size_type{0});
}

TEST(LinalgLevel1IdxAbsMax, ComplexUsesOneNormTerm) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {5.5, 0.0}};
    std::mdspan v(data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::vector_idx_abs_max(v), 0);
}

TEST(LinalgLevel1Reductions, UnsignedMagnitudesRemainWellFormed) {
    unsigned vector_data[] = {1u, 4u, 2u};
    unsigned matrix_data[] = {1u, 2u, 3u, 4u};
    std::mdspan vector(vector_data, std::extents<int, 3>{});
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});

    EXPECT_EQ(std::linalg::vector_abs_sum(vector), 7u);
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 1u);
    EXPECT_EQ(std::linalg::matrix_one_norm(matrix), 6u);
    EXPECT_EQ(std::linalg::matrix_inf_norm(matrix), 7u);
}

TEST(LinalgLevel1SumOfSquares, CombinesScaleAndScaledSum) {
    double data[] = {3.0, 4.0};
    std::mdspan v(data, std::extents<int, 2>{});

    auto result = std::linalg::vector_sum_of_squares(
        v, std::linalg::sum_of_squares_result<double>{});
    EXPECT_DOUBLE_EQ(result.scaling_factor, 4.0);
    EXPECT_DOUBLE_EQ(result.scaled_sum_of_squares, 1.5625);

    auto combined = std::linalg::vector_sum_of_squares(
        v, std::linalg::sum_of_squares_result<double>{2.0, 1.0});
    EXPECT_DOUBLE_EQ(combined.scaling_factor, 4.0);
    EXPECT_DOUBLE_EQ(combined.scaled_sum_of_squares, 1.8125);
}

TEST(LinalgLevel1MatrixNorms, RectangularRealMatrix) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix), std::sqrt(91.0), 1e-12);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix), 9.0);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix), 15.0);
}

TEST(LinalgLevel1MatrixNorms, OneNormAddsExplicitInit) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix, 10.0), 19.0);
}

TEST(LinalgLevel1MatrixNorms, InfinityNormAddsExplicitInit) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix, 10.0), 25.0);
}

TEST(LinalgLevel1MatrixNorms, ComplexMatrixUsesMagnitude) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {0.0, 0.0}, {1.0, -1.0}, {-2.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 2, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::matrix_frob_norm(matrix)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_one_norm(matrix)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_inf_norm(matrix)), double>);
    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix), std::sqrt(31.0), 1e-12);
    EXPECT_NEAR(std::linalg::matrix_one_norm(matrix), 5.0 + std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(std::linalg::matrix_inf_norm(matrix), 5.0, 1e-12);
}

TEST(LinalgLevel1MatrixNorms, OneAndInfinityNormAcceptComplexInit) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {0.0, 0.0}, {1.0, -1.0}, {-2.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 2, 2>{});
    const complex init{1.0, 2.0};

    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, init),
        (complex{6.0 + std::sqrt(2.0), 2.0}));
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, init),
        (complex{6.0, 2.0}));
}

TEST(LinalgLevel1MatrixNorms, FrobeniusInitUsesMagnitudeSquared) {
    using complex = std::complex<double>;
    complex data[] = {{4.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 1, 1>{});

    EXPECT_EQ(
        std::linalg::matrix_frob_norm(matrix, complex{0.0, 3.0}),
        (complex{5.0, 0.0}));
}

TEST(LinalgLevel1MatrixNorms, FrobeniusSquaresElementsInAccumulationType) {
    float data[] = {1.0e20f};
    std::mdspan matrix(data, std::extents<int, 1, 1>{});

    const double result = std::linalg::matrix_frob_norm(matrix, 0.0);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 1.0e20, 1.0e14);
}

TEST(LinalgLevel1MatrixNorms, FrobeniusNormAvoidsIntermediateOverflow) {
    double data[] = {1.0e200, 1.0e200};
    std::mdspan matrix(data, std::extents<int, 1, 2>{});

    const double result = std::linalg::matrix_frob_norm(matrix);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, std::sqrt(2.0) * 1.0e200, 1.0e185);
}

TEST(LinalgLevel1Givens, SetupAndApplyRotation) {
    auto rotation = std::linalg::setup_givens_rotation(3.0, 4.0);
    EXPECT_DOUBLE_EQ(rotation.r, 5.0);
    EXPECT_DOUBLE_EQ(rotation.c, 0.6);
    EXPECT_DOUBLE_EQ(rotation.s, 0.8);

    double x_data[] = {3.0, 0.0};
    double y_data[] = {4.0, 2.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);

    EXPECT_NEAR(x[0], 5.0, 1e-12);
    EXPECT_NEAR(y[0], 0.0, 1e-12);
    EXPECT_NEAR(x[1], 1.6, 1e-12);
    EXPECT_NEAR(y[1], 1.2, 1e-12);
}

TEST(LinalgLevel1Givens, ComplexRotationUsesRealCAndConjugateS) {
    using complex = std::complex<double>;
    const auto rotation = std::linalg::setup_givens_rotation(complex{1.0, 0.0}, complex{0.0, 1.0});
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    EXPECT_NEAR(rotation.c, inv_sqrt2, 1e-12);
    EXPECT_NEAR(rotation.s.real(), 0.0, 1e-12);
    EXPECT_NEAR(rotation.s.imag(), -inv_sqrt2, 1e-12);
    EXPECT_NEAR(rotation.r.real(), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(rotation.r.imag(), 0.0, 1e-12);

    complex x_data[] = {{1.0, 0.0}};
    complex y_data[] = {{0.0, 1.0}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);

    EXPECT_NEAR(x[0].real(), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(x[0].imag(), 0.0, 1e-12);
    EXPECT_NEAR(y[0].real(), 0.0, 1e-12);
    EXPECT_NEAR(y[0].imag(), 0.0, 1e-12);
}

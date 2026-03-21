#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <cmath>

TEST(LinalgLevel1, PlaceholderPassesUntilSIMD) { SUCCEED(); }

TEST(LinalgLevel1DotSIMD, DoubleEquivalence) {
    constexpr int N = 1024;
    static double x[N], y[N];
    for (int i = 0; i < N; ++i) { x[i] = static_cast<double>(i+1); y[i] = 1.0/(i+1); }
    std::mdspan xv(x, std::extents<int, N>{});
    std::mdspan yv(y, std::extents<int, N>{});
    double result = std::linalg::dot(xv, yv, 0.0);
    EXPECT_NEAR(result, static_cast<double>(N), 1e-10);
}

TEST(LinalgLevel1NormSIMD, DoubleEquivalence) {
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

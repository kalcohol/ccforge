#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {
namespace math = std::simd::detail::special_math;

template<class T>
void check_limits() {
    const T inf = std::numeric_limits<T>::infinity();
    for (unsigned n : {1u, 2u, 3u, 4u, 127u}) {
        const T positive = math::hermite_fallback(n, inf);
        const T negative = math::hermite_fallback(n, -inf);
        EXPECT_EQ(positive, inf);
        EXPECT_EQ(negative, (n & 1u) ? -inf : inf);
        EXPECT_EQ(math::laguerre_fallback(n, inf), (n & 1u) ? -inf : inf);
        EXPECT_EQ(math::assoc_laguerre_fallback(n, 7u, inf), (n & 1u) ? -inf : inf);
    }
    EXPECT_EQ(math::hermite_fallback(0, -inf), T{1});
    EXPECT_EQ(math::laguerre_fallback(0, inf), T{1});
    EXPECT_EQ(math::assoc_laguerre_fallback(0, 7, inf), T{1});
    EXPECT_TRUE(std::isnan(math::hermite_fallback(1024, inf)));
    EXPECT_TRUE(std::isnan(math::laguerre_fallback(1024, inf)));
    EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(1024, 7, inf)));
    EXPECT_TRUE(std::isnan(math::hermite_fallback(0, std::numeric_limits<T>::quiet_NaN())));
    EXPECT_EQ(math::hermite_fallback(3, T{2}), T{40});
    EXPECT_EQ(math::laguerre_fallback(2, T{2}), T{-1});
}

TEST(SimdMathPolynomialInfinity, FloatAndDoubleLimitsPreserveParity) {
    check_limits<float>();
    check_limits<double>();
}

TEST(SimdMathPolynomialInfinity, PublicOverloadsFollowSelectedScalarBackend) {
    using vector = std::simd::vec<double, 2>;
    const vector input(std::numeric_limits<double>::infinity());
    const auto hermite = std::simd::hermite(3u, input);
    const auto laguerre = std::simd::laguerre(3u, input);
#ifdef __cpp_lib_math_special_functions
    const double expected_hermite = std::hermite(3u, input[0]);
    const double expected_laguerre = std::laguerre(3u, input[0]);
#else
    const double expected_hermite = std::numeric_limits<double>::infinity();
    const double expected_laguerre = -std::numeric_limits<double>::infinity();
#endif
    for (std::simd::simd_size_type i = 0; i != vector::size; ++i) {
        if (std::isnan(expected_hermite)) {
            EXPECT_TRUE(std::isnan(hermite[i]));
        } else {
            EXPECT_EQ(hermite[i], expected_hermite);
            EXPECT_EQ(std::signbit(hermite[i]), std::signbit(expected_hermite));
        }
        if (std::isnan(expected_laguerre)) {
            EXPECT_TRUE(std::isnan(laguerre[i]));
        } else {
            EXPECT_EQ(laguerre[i], expected_laguerre);
            EXPECT_EQ(std::signbit(laguerre[i]), std::signbit(expected_laguerre));
        }
    }
}
}

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace {

namespace math = std::simd::detail::special_math;

TEST(SimdMathDegreeBudget, PolynomialFamiliesRejectTheFirstUnsupportedDegree) {
    constexpr unsigned limit = 1024u;
    EXPECT_TRUE(std::isnan(math::hermite_fallback(limit, 0.0)));
    EXPECT_TRUE(std::isnan(math::laguerre_fallback(limit, 0.0)));
    EXPECT_TRUE(std::isnan(math::legendre_fallback(limit, 1.0)));
    EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(limit, 0u, 0.0)));
    EXPECT_TRUE(std::isnan(math::assoc_legendre_fallback(limit, 0u, 1.0)));
    EXPECT_TRUE(std::isnan(math::sph_legendre_fallback(limit, 0u, 0.0)));
}

TEST(SimdMathDegreeBudget, AssociatedLaguerreBoundsItsIndependentOrder) {
    for (const unsigned order : {1024u, std::numeric_limits<unsigned>::max()}) {
        EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(0u, order, 0.0)));
        EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(1u, order, 0.0)));
        EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(3u, order, 0.0)));
    }
}

TEST(SimdMathDegreeBudget, MaximumUnsignedArgumentsReturnWithoutRecursion) {
    constexpr unsigned maximum = std::numeric_limits<unsigned>::max();
    EXPECT_TRUE(std::isnan(math::hermite_fallback(maximum, 0.0f)));
    EXPECT_TRUE(std::isnan(math::laguerre_fallback(maximum, 0.0f)));
    EXPECT_TRUE(std::isnan(math::legendre_fallback(maximum, 1.0f)));
    EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(maximum, maximum, 0.0f)));
    EXPECT_TRUE(std::isnan(math::assoc_legendre_fallback(maximum, maximum, 0.0f)));
    EXPECT_TRUE(std::isnan(math::sph_legendre_fallback(maximum, maximum, 0.0f)));
}

TEST(SimdMathDegreeBudget, LastSupportedDegreesKeepAnalyticControls) {
    EXPECT_EQ(math::laguerre_fallback(1023u, 0.0), 1.0);
    EXPECT_EQ(math::legendre_fallback(1023u, 1.0), 1.0);
    EXPECT_EQ(math::legendre_fallback(1023u, -1.0), -1.0);
    EXPECT_EQ(math::assoc_legendre_fallback(1023u, 0u, 1.0), 1.0);
    EXPECT_EQ(math::assoc_laguerre_fallback(0u, 1023u, 1.0), 1.0);
    EXPECT_EQ(math::assoc_laguerre_fallback(1u, 1023u, 0.0), 1024.0);
    EXPECT_EQ(math::assoc_legendre_fallback(3u,
        std::numeric_limits<unsigned>::max(), 0.5), 0.0);
    EXPECT_DOUBLE_EQ(math::hermite_fallback(3u, 0.5), -5.0);
    EXPECT_NEAR(math::sph_legendre_fallback(0u, 0u, 0.5),
        0.28209479177387814347, 2e-16);
}

} // namespace

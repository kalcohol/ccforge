#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

namespace math = std::simd::detail::special_math;

TEST(SimdMathSphericalDegreeBudget, FirstUnsupportedDegreeRejectsRegularAndZeroArguments) {
    for (const double x : {0.0, 1.0, 1000.0}) {
        EXPECT_TRUE(std::isnan(math::sph_bessel_fallback(1024u, x)));
        EXPECT_TRUE(std::isnan(math::sph_neumann_fallback(1024u, x)));
    }
}

TEST(SimdMathSphericalDegreeBudget, MaximumUnsignedDegreeReturnsWithoutRecurrence) {
    constexpr unsigned maximum = std::numeric_limits<unsigned>::max();
    EXPECT_TRUE(std::isnan(math::sph_bessel_fallback(maximum, 1.0f)));
    EXPECT_TRUE(std::isnan(math::sph_neumann_fallback(maximum, 1.0f)));
}

TEST(SimdMathSphericalDegreeBudget, LastSupportedDegreeAndAnalyticControlsRemainAvailable) {
    EXPECT_TRUE(std::isfinite(math::sph_bessel_fallback(1023u, 1e6)));
    EXPECT_TRUE(std::isfinite(math::sph_neumann_fallback(1023u, 1e6)));
    EXPECT_DOUBLE_EQ(math::sph_bessel_fallback(0u, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(math::sph_bessel_fallback(3u, 0.0), 0.0);
    EXPECT_EQ(math::sph_neumann_fallback(3u, 0.0),
        -std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(math::sph_bessel_fallback(0u, 1.0), std::sin(1.0));
    EXPECT_DOUBLE_EQ(math::sph_neumann_fallback(0u, 1.0), -std::cos(1.0));
}

} // namespace

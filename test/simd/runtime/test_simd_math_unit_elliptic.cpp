#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

namespace math = std::simd::detail::special_math;

// mpmath 1.3.0 at 100 and 160 decimal digits, with exact binary input ratios.
struct reference {
    double characteristic;
    double angle;
    long double value;
};

constexpr reference references[] = {
    {0x1.fffffffffffffp-1, 1.0,
        2.05433293325624845165388734565213265509L},
    {0x1.0000000000001p+0, 1.0,
        2.05433293325624910276958096932835295234L},
    {0x1.fffffffffdcd1p-1, 0x1.6666666666666p+0,
        18.2848934714086040642696232020416932535L},
    {0x1.0000000001198p+0, 0x1.6666666666666p+0,
        18.2848934719898786244504347415106931437L},
};

TEST(SimdMathUnitElliptic, CharacteristicNeighborsKeepIndependentReferences) {
    for (const auto& value : references) {
        SCOPED_TRACE(value.characteristic);
        const double actual = math::ellint_3_fallback(
            1.0, value.characteristic, value.angle);
        ASSERT_TRUE(std::isfinite(actual));
        EXPECT_NEAR(actual, value.value, 3e-13L * value.value);
        EXPECT_EQ(math::ellint_3_fallback(-1.0, value.characteristic, value.angle),
            actual);
        EXPECT_EQ(math::ellint_3_fallback(1.0, value.characteristic, -value.angle),
            -actual);
    }
}

TEST(SimdMathUnitElliptic, FloatNeighborsKeepIndependentReferences) {
    const float below = 0x1.fffffep-1f;
    const float above = 0x1.000002p+0f;
    EXPECT_NEAR(math::ellint_3_fallback(1.0f, below, 1.0f),
        2.05433281673456729261L, 8e-7L);
    EXPECT_NEAR(math::ellint_3_fallback(1.0f, above, 1.0f),
        2.05433316629967567089L, 8e-7L);
}

TEST(SimdMathUnitElliptic, ExactCharacteristicKeepsAnalyticAndRegularControls) {
    for (const double angle : {0.125, 0.5, 1.0, 1.4}) {
        const long double wide = angle;
        const long double cosine = std::cos(wide);
        const long double expected = (std::sin(wide) / (cosine * cosine) +
            std::asinh(std::tan(wide))) / 2;
        EXPECT_NEAR(math::ellint_3_fallback(1.0, 1.0, angle), expected,
            3e-13L * expected);
        EXPECT_NEAR(math::ellint_3_fallback(1.0, 0.0, angle),
            std::asinh(std::tan(wide)), 3e-13L);
    }
    EXPECT_NEAR(math::ellint_3_fallback(1.0, 0.5, 1.0),
        1.48309987342007733268876327765533750789L, 5e-13L);
    EXPECT_NEAR(math::ellint_3_fallback(1.0, -2.0, 1.0),
        0.819770432067529391647616075374825919510L, 5e-13L);
    EXPECT_EQ(math::ellint_3_fallback(1.0, 0.5, 2.0),
        std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(math::ellint_3_fallback(1.0, 2.0, 1.0)));
}

} // namespace

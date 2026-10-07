#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

namespace math = std::simd::detail::special_math;

template<class T>
struct order_reference {
    T order;
    T argument;
    long double y;
};

// mpmath 1.3.0, 100 decimal digits, from the exact binary input ratios.
constexpr order_reference<double> double_references[] = {
    {0x1.fffffffffffffp-1, 0x1.0624dd2f1a9fcp-11,
     -1273.240852484860690024295698L},
    {0x1.0000000000001p+0, 0x1.0624dd2f1a9fcp-11,
     -1273.240852484863962532518910L},
    {0x1.0000000000001p+1, 0x1.0624dd2f1a9fcp-10,
     -1273239.863045671964066042891L},
    {0x1.cd2b297d889bcp-54, 0x1.b7cdfd9d7bdbbp-34,
     -14.73251627269724217657784516L},
    {0x1.c25c268497682p-44, 0x1.ad7f29abcaf48p-24,
     -10.33490267942080527712816905L},
    {0x1.0000000000080p+0, 0x1.b7cdfd9d7bdbbp-34,
     -6366197723.680000432540134603L},
    {0x1.0000000000020p+0, 0x1.56e1fc2f8f359p-997,
     -6.366197723707065439268694943e299L},
    {0x1.fffffff800000p-1, 0x1.999999999999ap-4,
     -6.45895108034026124016611932L},
    {0x1.0000000200000p+1, 1.0,
     -1.650682608275079643486563267L},
    {0x1p-40, 0x1.c982eb8d417eap-1,
     -1.157391397378617736808241935e-12L},
};

constexpr order_reference<float> float_references[] = {
    {0x1.fffffep-1f, 0x1.4484cp-100f,
     -6.366171447731960460959885457e29L},
    {0x1.000002p+0f, 0x1.4f8b58p-17f,
     -63662.06713621446328855301859L},
    {0x1.000002p+1f, 0x1.0624dep-10f,
     -1273242.177799212014532916515L},
    {0x1p-45f, 0x1.4f8b58p-17f,
     -7.40316029978438315310291709L},
    {0x1.70ef54p-56f, 0x1.4f8b58p-17f,
     -7.403160299784338539802069596L},
};

template<class T>
long double tolerance(const order_reference<T>& reference) {
    const long double relative = std::numeric_limits<T>::digits <= 24
        ? 8.0L * std::numeric_limits<T>::epsilon() : 1.0e-12L;
    return relative * std::max(1.0L, std::abs(reference.y));
}

template<class T, std::size_t N>
void check_fallback(const order_reference<T> (&references)[N]) {
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.order);
        SCOPED_TRACE(reference.argument);
        const auto result = math::cyl_bessel_jy_fallback(
            reference.order, reference.argument);
        ASSERT_TRUE(result.converged);
        ASSERT_TRUE(std::isfinite(result.y));
        EXPECT_NEAR(static_cast<long double>(result.y), reference.y,
                    tolerance(reference));
        EXPECT_LT(result.y, T{});
    }
}

TEST(SimdMathOrder, DoubleFallbackPreservesOrdersNearIntegers) {
    check_fallback(double_references);
}

TEST(SimdMathOrder, FloatFallbackPreservesOrdersNearIntegers) {
    check_fallback(float_references);
}

TEST(SimdMathOrder, ExactIntegerControlsKeepTheirValues) {
    constexpr order_reference<double> references[] = {
        {0.0, 1e-10, -14.732516272697242019498212480778L},
        {1.0, 5e-4, -1273.2408524848617808603701024225L},
        {2.0, 1e-3, -1273239.8630456674272162327407159L},
    };
    check_fallback(references);
    EXPECT_NEAR(math::cyl_bessel_j_fallback(0.0, 1e-10), 1.0, 2e-15);
    EXPECT_NEAR(math::cyl_bessel_j_fallback(1.0, 5e-4),
                0.00024999999218750008658437784951754L, 1e-18L);
    EXPECT_NEAR(math::cyl_bessel_j_fallback(2.0, 1e-3),
                0.00000012499998958333366405833080188822L, 1e-21L);
}

TEST(SimdMathOrder, WellConditionedReflectionRemainsAccepted) {
    const auto result = math::cyl_bessel_reduced_series_pair<double>(0.25L, 0.001L);
    ASSERT_TRUE(result.converged);
    EXPECT_NEAR(result.j, 0.16497621310670325297862075054536L, 2e-14L);
    EXPECT_NEAR(result.y, -7.5527355812032834339152208712844L, 2e-12L);
}

TEST(SimdMathOrder, BoundedNearIntegerFallbackCoversSmallArgumentEndpoint) {
    const long double delta = 8.0L * std::numeric_limits<long double>::epsilon();
    const auto result = math::cyl_bessel_reduced_series_pair<double>(delta, 2.0L);
    ASSERT_TRUE(result.converged);
    EXPECT_NEAR(result.y, 0.510375672649745119596606592727L, 2e-12L);
}

TEST(SimdMathOrder, UncertifiedReflectionDoesNotClaimConvergence) {
    const long double delta = 8.0L * std::numeric_limits<long double>::epsilon();
    const auto rejected = math::cyl_bessel_reduced_series_pair<double>(delta, 3.0L);
    EXPECT_FALSE(rejected.converged);
    const auto completed = math::cyl_bessel_reduced_pair<double>(delta, 3.0L);
    ASSERT_TRUE(completed.converged);
    EXPECT_NEAR(completed.y, 0.37685001001279038196711019239662L, 2e-12L);
}

TEST(SimdMathOrder, NearIntegerSeriesUsesEachValuesErrorBudget) {
    const long double delta = 0x1p-54L;
    const std::array result{
        math::cyl_bessel_reduced_series_pair<double>(delta, 1e-10L),
        math::cyl_bessel_reduced_series_pair<double>(delta + 1.0L, 1e-10L)};
    const long double target = math::cyl_bessel_target_tolerance<double>();
    for (const auto& value : result) {
        ASSERT_TRUE(value.converged);
        EXPECT_GE(value.error, 0.0L);
        EXPECT_LE(value.error, target * std::max(1.0L, std::abs(value.y)));
    }
    EXPECT_NEAR(result[0].y, -14.732516272697242L, 2e-12L);
    EXPECT_GT(std::abs(result[1].y), 1e9L);
    EXPECT_LT(result[0].error, 1e-10L);
}

#ifndef __cpp_lib_math_special_functions
template<class T, std::size_t N>
void check_public(const order_reference<T> (&references)[N]) {
    using vector = std::simd::vec<T, 2>;
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.order);
        SCOPED_TRACE(reference.argument);
        const auto result = std::simd::cyl_neumann(
            reference.order, vector(reference.argument));
        for (std::simd::simd_size_type lane = 0; lane < vector::size; ++lane) {
            ASSERT_TRUE(std::isfinite(result[lane]));
            EXPECT_NEAR(static_cast<long double>(result[lane]), reference.y,
                        tolerance(reference));
        }
    }
}

TEST(SimdMathOrder, PublicFloatAndDoubleApisExerciseForgeFallback) {
    check_public(float_references);
    check_public(double_references);
}
#endif

} // namespace

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace {

namespace math = std::simd::detail::special_math;

struct phase_reference {
    double order;
    double argument;
    double j;
    double y;
};

// mpmath 1.3.0, 180 decimal digits, using the exact binary double inputs.
constexpr phase_reference references[] = {
    {0.0, 1e15, 6.1566386468850216773261e-9, 2.4468665123771323386811e-8},
    {0.25, 1e17, -1.9379120531015773425543e-9, 1.6157644005607509079094e-9},
    {0.5, 1e19, -2.3391032242243256593477e-10, 9.4593542596689839201111e-11},
    {11.0, 1e19, 9.8511839747855882221227e-11, -2.3228731060101172952127e-10},
    {5.25, 1e30, 7.3996636483350962619805e-16, 2.9844522325321720385552e-16},
    {0.25, 1e100, 1.7659073920944932329813e-53, -7.9788260661494801065949e-51},
};

TEST(SimdMathPhase, FallbackRetainsBoundedPhaseAtLargeArguments) {
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.argument);
        const double amplitude = std::hypot(reference.j, reference.y);
        const auto result = math::cyl_bessel_jy_fallback(
            reference.order, reference.argument);
        ASSERT_TRUE(result.converged);
        EXPECT_NEAR(result.j, reference.j, amplitude * 3e-12);
        EXPECT_NEAR(result.y, reference.y, amplitude * 3e-12);
    }
}

template<class T, std::size_t N>
void check_half_integer_identity(const std::array<T, N>& arguments) {
    for (T x : arguments) {
        SCOPED_TRACE(static_cast<long double>(x));
        const long double argument = static_cast<long double>(x);
        const long double scale = std::sqrt(
            2.0L / (math::pi_v<long double> * argument));
        const long double tolerance = scale *
            std::max(3e-12L, 16.0L * std::numeric_limits<T>::epsilon());
        const auto result = math::cyl_bessel_jy_fallback(T{0.5}, x);
        ASSERT_TRUE(result.converged);
        EXPECT_NEAR(static_cast<long double>(result.j),
                    scale * std::sin(argument), tolerance);
        EXPECT_NEAR(static_cast<long double>(result.y),
                    -scale * std::cos(argument), tolerance);
    }
}

TEST(SimdMathPhase, HalfIntegerIdentityCoversFloatDoubleAndLongDouble) {
    check_half_integer_identity(std::array{1e15f, 1e19f, 1e30f});
    check_half_integer_identity(std::array{1e15, 1e19, 1e30, 1e100});
    check_half_integer_identity(std::array{1e15L, 1e19L, 1e30L, 1e100L});
}

TEST(SimdMathPhase, HalfIntegerHankelCoefficientsHaveNoPhaseSubtraction) {
    for (long double order : {-0.5L, 0.5L}) {
        const auto result = math::cyl_bessel_hankel_asymptotic(order, 1e30L);
        ASSERT_TRUE(result.converged);
        const long double scale = std::sqrt(
            2.0L / (math::pi_v<long double> * 1e30L));
        const long double expected_j = order < 0
            ? scale * std::cos(1e30L) : scale * std::sin(1e30L);
        const long double expected_y = order < 0
            ? scale * std::sin(1e30L) : -scale * std::cos(1e30L);
        EXPECT_NEAR(result.j, expected_j,
                    scale * 16 * std::numeric_limits<long double>::epsilon());
        EXPECT_NEAR(result.y, expected_y,
                    scale * 16 * std::numeric_limits<long double>::epsilon());
    }
}

#ifndef __cpp_lib_math_special_functions
TEST(SimdMathPhase, PublicSimdUsesTheFallbackPhase) {
    using vector = std::simd::vec<double, 2>;
    for (const auto& reference : references) {
        const vector arguments(reference.argument);
        const auto j = std::simd::cyl_bessel_j(reference.order, arguments);
        const auto y = std::simd::cyl_neumann(reference.order, arguments);
        const double tolerance = std::hypot(reference.j, reference.y) * 3e-12;
        for (std::simd::simd_size_type i = 0; i < vector::size; ++i) {
            EXPECT_NEAR(j[i], reference.j, tolerance);
            EXPECT_NEAR(y[i], reference.y, tolerance);
        }
    }
}
#endif

} // namespace

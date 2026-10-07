#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's ellint_3 infinity guard."
#endif

namespace {

namespace math = std::simd::detail::special_math;

template<class T>
std::array<T, 10> finite_characteristics() {
    return {std::numeric_limits<T>::lowest(), T{-1000000}, T{-3}, T{-0.5},
        -std::numeric_limits<T>::denorm_min(), -T{}, T{}, T{0.5},
        T{0.75}, std::nextafter(T{1}, T{})};
}

template<class T>
void expect_signed_infinity(T actual, T expected) {
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(std::isinf(actual));
    EXPECT_EQ(std::signbit(actual), std::signbit(expected));
}

template<class T>
void check_zero_modulus_infinity() {
    const T inf = std::numeric_limits<T>::infinity();
    for (T nu : finite_characteristics<T>()) {
        SCOPED_TRACE(nu);
        for (T k : {T{}, -T{}}) {
            for (T phi : {inf, -inf}) {
                expect_signed_infinity(math::ellint_3_fallback(k, nu, phi), phi);
                // The zero-modulus route must agree with the existing nonzero route.
                expect_signed_infinity(math::ellint_3_fallback(
                    std::numeric_limits<T>::min(), nu, phi), phi);
            }
        }
    }
}

template<class T>
void check_domain_and_poles() {
    const T inf = std::numeric_limits<T>::infinity();
    const T nan = std::numeric_limits<T>::quiet_NaN();
    const T above_one = std::nextafter(T{1}, inf);
    for (T phi : {inf, -inf}) {
        for (T k : {T{}, -T{}, T{0.5}, T{1}, T{-1}}) {
            EXPECT_TRUE(std::isnan(math::ellint_3_fallback(k, nan, phi)));
            for (T nu : {above_one, T{2}, inf}) {
                EXPECT_TRUE(std::isnan(math::ellint_3_fallback(k, nu, phi)));
            }
            expect_signed_infinity(math::ellint_3_fallback(k, T{1}, phi), phi);
        }
        for (T k : {nan, T{2}, T{-2}, inf, -inf}) {
            EXPECT_TRUE(std::isnan(math::ellint_3_fallback(k, T{0.5}, phi)));
        }
        // Nonfinite characteristics remain outside the new acceptance condition.
        EXPECT_TRUE(std::isnan(math::ellint_3_fallback(T{}, -inf, phi)));
        expect_signed_infinity(math::ellint_3_fallback(T{1}, T{-3}, phi), phi);
    }
    EXPECT_TRUE(std::isnan(math::ellint_3_fallback(T{}, T{0.5}, nan)));
}

template<class T>
void check_finite_controls() {
    const T tolerance = T{64} * std::numeric_limits<T>::epsilon();
    const T half_pi = math::pi_v<T> / T{2};
    for (T k : {T{}, -T{}}) {
        for (T phi : {T{}, -T{}}) {
            const T actual = math::ellint_3_fallback(k, T{0.5}, phi);
            EXPECT_EQ(actual, phi);
            EXPECT_EQ(std::signbit(actual), std::signbit(phi));
        }
        for (T sign : {T{1}, T{-1}}) {
            // Pi(0, -3, pi/4) = atan(2)/2; one full period adds pi/2.
            const T principal = std::atan(T{2}) / T{2};
            EXPECT_NEAR(math::ellint_3_fallback(k, T{-3}, sign * half_pi / T{2}),
                sign * principal, tolerance);
            EXPECT_NEAR(math::ellint_3_fallback(k, T{-3},
                sign * (math::pi_v<T> + half_pi / T{2})),
                sign * (half_pi + principal), T{4} * tolerance);
            EXPECT_NEAR(math::ellint_3_fallback(k, T{}, sign * T{0.5}),
                sign * T{0.5}, tolerance);
            EXPECT_NEAR(math::ellint_3_fallback(k, T{1}, sign * T{0.5}),
                sign * std::tan(T{0.5}), tolerance);
            const T pole = std::asin(T{1} / std::sqrt(T{2}));
            expect_signed_infinity(math::ellint_3_fallback(k, T{2}, sign * pole),
                sign * std::numeric_limits<T>::infinity());
            EXPECT_TRUE(std::isnan(math::ellint_3_fallback(k, T{2},
                sign * (pole + T{0.125}))));
        }
    }
}

#ifndef __cpp_lib_math_special_functions
template<class T>
void check_public_infinity() {
    using vector = std::simd::vec<T, 4>;
    const T inf = std::numeric_limits<T>::infinity();
    const vector moduli([](auto i) { return i % 2 == 0 ? T{} : -T{}; });
    const vector amplitudes([inf](auto i) { return i % 2 == 0 ? inf : -inf; });
    for (T nu : finite_characteristics<T>()) {
        SCOPED_TRACE(nu);
        const auto scalar_order = std::simd::ellint_3(moduli, nu, amplitudes);
        const auto vector_order = std::simd::ellint_3(T{}, vector(nu), amplitudes);
        for (std::simd::simd_size_type i = 0; i < vector::size; ++i) {
            expect_signed_infinity(scalar_order[i], amplitudes[i]);
            expect_signed_infinity(vector_order[i], amplitudes[i]);
        }
    }
}
#endif

TEST(SimdMathEllint3Infinity, ZeroModulusReturnsSignedInfinityForFiniteCharacteristic) {
    check_zero_modulus_infinity<float>();
    check_zero_modulus_infinity<double>();
    check_zero_modulus_infinity<long double>();
}

TEST(SimdMathEllint3Infinity, DomainAndPoleClassificationStillTakePrecedence) {
    check_domain_and_poles<float>();
    check_domain_and_poles<double>();
    check_domain_and_poles<long double>();
}

TEST(SimdMathEllint3Infinity, FiniteAnglesAndSignedZeroRemainUnchanged) {
    check_finite_controls<float>();
    check_finite_controls<double>();
    check_finite_controls<long double>();
}

TEST(SimdMathEllint3Infinity, PublicFallbackPreservesSignedInfiniteLanes) {
#ifdef __cpp_lib_math_special_functions
    GTEST_SKIP() << "Native scalar special-math dispatch does not exercise this fallback.";
#else
    check_public_infinity<float>();
    check_public_infinity<double>();
#endif
}

} // namespace

#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's beta infinity guard."
#endif

namespace {

namespace math = std::simd::detail::special_math;

// Direct fallback calls keep native special-math dispatch out of these checks.
template<class T>
void expect_positive_zero(T x, T y) {
    const T result = math::beta_fallback(x, y);
    EXPECT_EQ(result, T{});
    EXPECT_FALSE(std::signbit(result));
}

template<class T>
void check_both_infinite() {
    const T inf = std::numeric_limits<T>::infinity();
    expect_positive_zero(inf, inf);
}

template<class T>
void check_positive_finite_and_infinite() {
    const T inf = std::numeric_limits<T>::infinity();
    const std::array<T, 8> positive{
        std::numeric_limits<T>::denorm_min(),
        std::numeric_limits<T>::min(),
        T{0.5}, T{1}, T{2}, T{127}, T{10000000},
        std::numeric_limits<T>::max()};
    for (T value : positive) {
        SCOPED_TRACE(value);
        expect_positive_zero(inf, value);
        expect_positive_zero(value, inf);
    }
}

template<class T>
void check_domain_precedence() {
    const T inf = std::numeric_limits<T>::infinity();
    const std::array<T, 7> invalid{
        T{}, -T{}, T{-1}, -std::numeric_limits<T>::min(),
        -std::numeric_limits<T>::max(), -inf,
        std::numeric_limits<T>::quiet_NaN()};
    for (T value : invalid) {
        EXPECT_TRUE(std::isnan(math::beta_fallback(value, inf)));
        EXPECT_TRUE(std::isnan(math::beta_fallback(inf, value)));
        EXPECT_TRUE(std::isnan(math::beta_fallback(value, T{1})));
        EXPECT_TRUE(std::isnan(math::beta_fallback(T{1}, value)));
    }
}

template<class T>
void check_finite_controls() {
    for (T value : {T{0.5}, T{1}, T{2}, T{8}, T{100000000}}) {
        EXPECT_EQ(math::beta_fallback(T{1}, value), T{1} / value);
        EXPECT_EQ(math::beta_fallback(value, T{1}), T{1} / value);
    }
    const T tolerance = T{128} * std::numeric_limits<T>::epsilon();
    EXPECT_NEAR(math::beta_fallback(T{2}, T{3}), T{1} / T{12}, tolerance);
    EXPECT_NEAR(math::beta_fallback(T{2}, T{2}), T{1} / T{6}, tolerance);
    EXPECT_NEAR(math::beta_fallback(T{0.5}, T{0.5}), math::pi_v<T>,
        tolerance * math::pi_v<T>);
    EXPECT_EQ(math::beta_fallback(T{0.5}, T{3}), math::beta_fallback(T{3}, T{0.5}));
    const T expected = static_cast<T>(1.0L / (1.0e8L * (1.0e8L + 1.0L)));
    EXPECT_NEAR(math::beta_fallback(T{100000000}, T{2}), expected, tolerance * expected);
}

TEST(SimdMathBetaInfinity, BothArgumentsInfiniteReturnPositiveZero) {
    check_both_infinite<float>();
    check_both_infinite<double>();
    check_both_infinite<long double>();
}

TEST(SimdMathBetaInfinity, PositiveFiniteArgumentAndInfinityReturnPositiveZero) {
    check_positive_finite_and_infinite<float>();
    check_positive_finite_and_infinite<double>();
    check_positive_finite_and_infinite<long double>();
}

TEST(SimdMathBetaInfinity, InvalidDomainTakesPrecedenceOverInfinity) {
    check_domain_precedence<float>();
    check_domain_precedence<double>();
    check_domain_precedence<long double>();
}

TEST(SimdMathBetaInfinity, FiniteControlsRemainUnchanged) {
    check_finite_controls<float>();
    check_finite_controls<double>();
    check_finite_controls<long double>();
}

} // namespace

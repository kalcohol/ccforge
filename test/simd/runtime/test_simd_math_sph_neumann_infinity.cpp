#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's sph_neumann infinity guard."
#endif

namespace {

namespace math = std::simd::detail::special_math;

// Direct fallback calls keep native special-math dispatch out of these checks.
template<class T>
void check_positive_infinity() {
    const T inf = std::numeric_limits<T>::infinity();
    const std::array<unsigned, 5> orders{
        0u, 1u, 2u, 127u, math::recurrence_order_limit - 1u};
    for (unsigned n : orders) {
        SCOPED_TRACE(n);
        const T result = math::sph_neumann_fallback(n, inf);
        EXPECT_EQ(result, T{});
        EXPECT_FALSE(std::signbit(result));
    }
}

template<class T>
void check_invalid_domain_and_order_budget() {
    const T inf = std::numeric_limits<T>::infinity();
    const std::array<T, 3> invalid{
        std::numeric_limits<T>::quiet_NaN(), -inf, T{-1}};
    for (unsigned n : {0u, 1u, 127u}) {
        for (T x : invalid) {
            EXPECT_TRUE(std::isnan(math::sph_neumann_fallback(n, x)));
        }
    }
    const std::array<unsigned, 3> invalid_orders{
        math::recurrence_order_limit,
        math::recurrence_order_limit + 1u,
        std::numeric_limits<unsigned>::max()};
    for (unsigned n : invalid_orders) {
        SCOPED_TRACE(n);
        for (T x : {T{}, T{1}, inf}) {
            EXPECT_TRUE(std::isnan(math::sph_neumann_fallback(n, x)));
        }
    }
}

template<class T>
void check_signed_zero() {
    const T inf = std::numeric_limits<T>::infinity();
    for (unsigned n : {0u, 1u, 127u, math::recurrence_order_limit - 1u}) {
        for (T x : {T{}, -T{}}) {
            const T result = math::sph_neumann_fallback(n, x);
            EXPECT_EQ(result, -inf);
            EXPECT_TRUE(std::signbit(result));
        }
    }
}

template<class T>
void check_finite_low_orders() {
    for (T x : {T{0.5}, T{1}, T{2}, T{8}}) {
        SCOPED_TRACE(x);
        EXPECT_EQ(math::sph_neumann_fallback(0u, x), -std::cos(x) / x);
        EXPECT_EQ(math::sph_neumann_fallback(1u, x),
            -std::cos(x) / (x * x) - std::sin(x) / x);
        const T inverse = T{1} / x;
        const T expected =
            (inverse - T{3} * inverse * inverse * inverse) * std::cos(x) -
            T{3} * inverse * inverse * std::sin(x);
        const T tolerance = T{64} * std::numeric_limits<T>::epsilon() *
            (T{1} + std::abs(expected));
        EXPECT_NEAR(math::sph_neumann_fallback(2u, x), expected, tolerance);
    }
}

TEST(SimdMathSphNeumannInfinity, PositiveInfinityReturnsPositiveZero) {
    check_positive_infinity<float>();
    check_positive_infinity<double>();
    check_positive_infinity<long double>();
}

TEST(SimdMathSphNeumannInfinity, InvalidDomainAndOrderBudgetRemainRejected) {
    check_invalid_domain_and_order_budget<float>();
    check_invalid_domain_and_order_budget<double>();
    check_invalid_domain_and_order_budget<long double>();
}

TEST(SimdMathSphNeumannInfinity, SignedZeroStillReturnsNegativeInfinity) {
    check_signed_zero<float>();
    check_signed_zero<double>();
    check_signed_zero<long double>();
}

TEST(SimdMathSphNeumannInfinity, FiniteLowOrderControlsRemainUnchanged) {
    check_finite_low_orders<float>();
    check_finite_low_orders<double>();
    check_finite_low_orders<long double>();
}

} // namespace

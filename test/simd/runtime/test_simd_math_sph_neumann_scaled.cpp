#ifndef FORGE_SM06_PRIVATE_KERNEL_PROBE
#include <simd>
#include <gtest/gtest.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>

#ifdef FORGE_SM06_PRIVATE_KERNEL_PROBE
// Standalone MSVC mode exercises the actual 53-bit working type.
namespace std::simd {
#include "common.hpp"
#include "bessel.hpp"
}
static_assert(std::numeric_limits<long double>::digits == 53 &&
              std::numeric_limits<long double>::radix == 2,
              "SM06 private-kernel probe requires binary 53-bit long double");
#else
#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's scaled spherical Neumann fallback."
#endif
#endif

namespace {

namespace math = std::simd::detail::special_math;

#ifdef FORGE_SM06_PRIVATE_KERNEL_PROBE
unsigned checked = 0u;
unsigned failures = 0u;
#endif

void check(bool passed, const char* label) {
#ifdef FORGE_SM06_PRIVATE_KERNEL_PROBE
    ++checked;
    if (!passed) {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
#else
    EXPECT_TRUE(passed) << label;
#endif
}

template<class T>
void check_finite(unsigned n, T x, long double expected) {
    const T result = math::sph_neumann_fallback(n, x);
    const long double tolerance =
        128.0L * std::numeric_limits<T>::epsilon() * std::abs(expected);
    check(std::isfinite(result), "finite spherical Neumann classification");
    check(std::abs(static_cast<long double>(result) - expected) <= tolerance,
          "finite spherical Neumann reference");
}

template<class T>
void check_negative_overflow(unsigned n, T x) {
    const T result = math::sph_neumann_fallback(n, x);
    check(std::isinf(result) && std::signbit(result),
          "true overflow must be negative infinity, not NaN");
}

struct reference {
    unsigned order;
    long double argument;
    long double value;
};

// DLMF 10.49.4 finite sums, with Decimal sine/cosine series at 100/160 digits.
// Each input is exactly binary representable; both precisions agree as printed.
constexpr reference normal_references[] = {
    {0u, 1.0L, -5.4030230586813971740093660744297660373e-1L},
    {1u, 1.0L, -1.3817732906760362240534389290732756034e+0L},
    {2u, 1.0L, -3.6050175661599689547593801797768502063e+0L},
    {3u, 0.5L, -2.4613004692361646071336479819786462862e+2L},
    {8u, 4.0L, -1.3523258836415108203715653273358304091e+1L},
    {20u, 0.5L, -6.7288761838234723020528666591065106691e+29L},
    {32u, 16.0L, -1.6951993571189261213864081994113816719e+5L},
    {64u, 64.0L, -3.3814483145586539311912152939925797361e-2L},
    {127u, 128.0L, -1.5481166688660126080068333025202692931e-2L},
};

template<class T>
void check_normal_references() {
    for (const auto& item : normal_references) {
        check_finite(item.order, static_cast<T>(item.argument), item.value);
    }
}

template<class T>
void check_tiny_arguments() {
    for (T x : {std::numeric_limits<T>::denorm_min(),
                std::numeric_limits<T>::min()}) {
        check(x > T{}, "mandatory positive tiny argument");
        for (unsigned n : {1u, 2u, 3u, 17u, 127u,
                           math::recurrence_order_limit - 1u}) {
            check_negative_overflow(n, x);
        }
    }
    check_negative_overflow(0u, std::numeric_limits<T>::denorm_min());
    check_finite(0u, std::numeric_limits<T>::min(),
                 -1.0L / static_cast<long double>(std::numeric_limits<T>::min()));
}

template<class T>
void check_finite_after_square_overflow() {
    const T x = std::ldexp(T{1}, std::numeric_limits<T>::max_exponent / 2 + 1);
    const T square = x * x;
    check(std::isfinite(x) && std::isinf(square),
          "finite input with overflowing unscaled seed denominator");
    // These low-order formulas use division before multiplication/squaring.
    const T first = -std::cos(x) / x / x - std::sin(x) / x;
    const T second = (T{3} / x) * first + std::cos(x) / x;
    check_finite(1u, x, static_cast<long double>(first));
    check_finite(2u, x, static_cast<long double>(second));
}

void check_finite_seeds_and_overflow_boundary() {
    check_finite(1u, 0x1.8p-64f,
                 -1.5123660752041709487261093663634142731e+38L);
    check_finite(1u, 0x1.8p-512,
                 -7.9897472660547373676858008479512210383e+307L);
    check_finite(28u, 1.0f,
                 -8.7667141572907966595585622954601566184e+36L);
    check_finite(150u, 1.0,
                 -3.7595557758175846400039765291418404199e+306L);
    for (unsigned n : {29u, 30u, 31u, 127u}) {
        check_negative_overflow(n, 1.0f);
    }
    for (unsigned n : {151u, 152u, 153u, math::recurrence_order_limit - 1u}) {
        check_negative_overflow(n, 1.0);
    }
}

#ifndef FORGE_SM06_PRIVATE_KERNEL_PROBE
TEST(SimdMathSphNeumannScaled, TinyArgumentsRemainSignedInfinities) {
    check_tiny_arguments<float>();
    check_tiny_arguments<double>();
    check_tiny_arguments<long double>();
}

TEST(SimdMathSphNeumannScaled, FiniteResultsSurviveSeedSquareOverflow) {
    check_finite_after_square_overflow<float>();
    check_finite_after_square_overflow<double>();
    check_finite_after_square_overflow<long double>();
}

TEST(SimdMathSphNeumannScaled, FiniteSeedsAndTrueOverflowRemainDistinct) {
    check_finite_seeds_and_overflow_boundary();
}

TEST(SimdMathSphNeumannScaled, NormalFiniteReferencesRemainAccurate) {
    check_normal_references<float>();
    check_normal_references<double>();
    check_normal_references<long double>();
}
#endif

} // namespace

#ifdef FORGE_SM06_PRIVATE_KERNEL_PROBE
int main() {
    check_tiny_arguments<float>();
    check_tiny_arguments<double>();
    check_tiny_arguments<long double>();
    check_finite_after_square_overflow<float>();
    check_finite_after_square_overflow<double>();
    check_finite_after_square_overflow<long double>();
    check_finite_seeds_and_overflow_boundary();
    check_normal_references<float>();
    check_normal_references<double>();
    check_normal_references<long double>();
    std::printf("SM06 private kernels: %d work bits, %u checks, %u failures, no skips\n",
                std::numeric_limits<long double>::digits, checked, failures);
    return checked == 136u && failures == 0u ? 0 : 1;
}
#endif

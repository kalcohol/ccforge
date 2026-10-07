#ifndef FORGE_SM06_POLYNOMIAL_PRIVATE_KERNEL_PROBE
#include "simd_test_common.hpp"

#include <gtest/gtest.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <type_traits>

#ifdef FORGE_SM06_POLYNOMIAL_PRIVATE_KERNEL_PROBE
// Standalone MSVC mode includes only the two actual private kernel headers.
namespace std::simd {
#include "common.hpp"
#include "polynomial.hpp"
}
static_assert(std::numeric_limits<long double>::digits == 53 &&
              std::numeric_limits<long double>::radix == 2 &&
              std::numeric_limits<long double>::max_exponent == 1024,
              "SM06 polynomial probe requires binary64 long double");

namespace {
unsigned private_checked = 0u;
unsigned private_failures = 0u;
const char* private_case = "unselected";

bool private_expect(bool passed, const char* expression, int line) {
    ++private_checked;
    if (!passed) {
        ++private_failures;
        std::printf("FAIL %s line %d: %s\n", private_case, line, expression);
    }
    return passed;
}
}

#define TEST(Suite, Name) void Suite##_##Name()
#define EXPECT_TRUE(value) \
    do { private_expect(static_cast<bool>(value), #value, __LINE__); } while (false)
#define EXPECT_EQ(left, right) \
    do { private_expect((left) == (right), #left " == " #right, __LINE__); } while (false)
#define EXPECT_LE(left, right) \
    do { private_expect((left) <= (right), #left " <= " #right, __LINE__); } while (false)
#define ASSERT_TRUE(value) \
    do { if (!private_expect(static_cast<bool>(value), #value, __LINE__)) return; } while (false)
#define ASSERT_NE(left, right) \
    do { if (!private_expect((left) != (right), #left " != " #right, __LINE__)) return; } while (false)
#endif

namespace {

namespace math = std::simd::detail::special_math;

// Independent mpmath references agree at 100 and 160 decimal digits.
// Associated Legendre references omit the Condon-Shortley phase, as C++ does.
template<class T>
void expect_relative(T actual, long double expected, long double tolerance) {
    ASSERT_TRUE(std::isfinite(actual));
    ASSERT_NE(actual, T{});
    EXPECT_LE(std::abs(static_cast<long double>(actual) / expected - 1.0L), tolerance);
}

TEST(SimdMathPolynomialScaling, ReportedFloatResultsRemainRepresentable) {
    EXPECT_EQ(math::hermite_fallback(51u, 0.0f), 0.0f);
    expect_relative(math::assoc_laguerre_fallback(50u, 10u, 200.0f),
        3.32888592429968127868390631133283872650e38L, 3e-6L);
    EXPECT_EQ(math::assoc_legendre_fallback(30u, 29u, 0.0f), 0.0f);
}

TEST(SimdMathPolynomialScaling, FloatNonzeroResultsSurviveOversizedIntermediateTerms) {
    expect_relative(math::hermite_fallback(51u, 1e-7f),
        -1.99999712089586339869978623277427191762e34L, 3e-6L);
    expect_relative(math::hermite_fallback(51u, -1e-7f),
        1.99999712089586339869978623277427191762e34L, 3e-6L);
    expect_relative(math::assoc_legendre_fallback(30u, 29u, 1e-7f),
        2.92156067128853553533482828922983151255e33L, 3e-6L);
    expect_relative(math::assoc_legendre_fallback(30u, 29u, -1e-7f),
        -2.92156067128853553533482828922983151255e33L, 3e-6L);
    expect_relative(math::laguerre_fallback(100u, 180.0f),
        7.15009792722553304248037978558642056957e36L, 3e-6L);
}

TEST(SimdMathPolynomialScaling, DoubleResultsDoNotRequireAWiderLongDoubleRange) {
    expect_relative(math::hermite_fallback(301u, 1e-100),
        3.22482479313209380254805406277716615401e254L, 2e-12L);
    expect_relative(math::hermite_fallback(401u, 1e-300),
        6.51178279557287419249486987633190300754e196L, 2e-12L);
    expect_relative(math::assoc_legendre_fallback(172u, 171u, 1e-100),
        5.49301804843003796986449807797434860386e261L, 2e-12L);
    expect_relative(math::laguerre_fallback(1000u, 1420.0),
        -3.97219102141256760897412813903336452253e306L, 2e-12L);
    expect_relative(math::assoc_laguerre_fallback(300u, 10u, 1400.0),
        3.22842159124789787712652741729754055596e289L, 2e-12L);
}

TEST(SimdMathPolynomialScaling, ZeroParitySurvivesUnrepresentableNeighbors) {
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(math::hermite_fallback(50u, 0.0f), -inf);
    EXPECT_EQ(math::hermite_fallback(51u, 0.0f), 0.0f);
    EXPECT_EQ(math::hermite_fallback(52u, 0.0f), inf);
    EXPECT_EQ(math::hermite_fallback(53u, 0.0f), 0.0f);
    EXPECT_EQ(math::assoc_legendre_fallback(29u, 29u, 0.0f), inf);
    EXPECT_EQ(math::assoc_legendre_fallback(30u, 29u, 0.0f), 0.0f);
    EXPECT_EQ(math::assoc_legendre_fallback(31u, 29u, 0.0f), -inf);
    EXPECT_EQ(math::assoc_legendre_fallback(32u, 29u, 0.0f), 0.0f);
    const double double_inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(math::hermite_fallback(300u, 0.0), double_inf);
    EXPECT_EQ(math::hermite_fallback(301u, 0.0), 0.0);
    EXPECT_EQ(math::hermite_fallback(302u, 0.0), -double_inf);
    EXPECT_EQ(math::hermite_fallback(303u, 0.0), 0.0);
    EXPECT_EQ(math::assoc_legendre_fallback(172u, 171u, 0.0), 0.0);
    EXPECT_EQ(math::assoc_legendre_fallback(174u, 171u, 0.0), 0.0);
    EXPECT_EQ(math::hermite_fallback(1023u, 0.0L), 0.0L);
    EXPECT_EQ(math::assoc_legendre_fallback(1023u, 1022u, 0.0L), 0.0L);
}

TEST(SimdMathPolynomialScaling, GenuineOverflowHasThePolynomialSignInsteadOfNaN) {
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(math::hermite_fallback(3u, 1e20f), inf);
    EXPECT_EQ(math::hermite_fallback(3u, -1e20f), -inf);
    EXPECT_EQ(math::hermite_fallback(4u, -1e20f), inf);
    EXPECT_EQ(math::laguerre_fallback(4u, 1e20f), inf);
    EXPECT_EQ(math::laguerre_fallback(5u, 1e20f), -inf);
    EXPECT_EQ(math::assoc_laguerre_fallback(4u, 10u, 1e20f), inf);
    EXPECT_EQ(math::assoc_laguerre_fallback(5u, 10u, 1e20f), -inf);

    const long double wide_max = std::numeric_limits<long double>::max();
    const long double wide_inf = std::numeric_limits<long double>::infinity();
    EXPECT_EQ(math::hermite_fallback(3u, wide_max), wide_inf);
    EXPECT_EQ(math::hermite_fallback(3u, -wide_max), -wide_inf);
    EXPECT_EQ(math::hermite_fallback(4u, -wide_max), wide_inf);
    EXPECT_EQ(math::laguerre_fallback(4u, wide_max), wide_inf);
    EXPECT_EQ(math::laguerre_fallback(5u, wide_max), -wide_inf);
    EXPECT_EQ(math::assoc_laguerre_fallback(4u, 10u, wide_max), wide_inf);
    EXPECT_EQ(math::assoc_laguerre_fallback(5u, 10u, wide_max), -wide_inf);
}

TEST(SimdMathPolynomialScaling, SubnormalArgumentsAndResultsAreNotDiscarded) {
    const double tiny = std::numeric_limits<double>::denorm_min();
    EXPECT_EQ(math::hermite_fallback(3u, tiny), -12.0 * tiny);
    EXPECT_EQ(math::assoc_legendre_fallback(2u, 1u, tiny), 3.0 * tiny);
    expect_relative(math::hermite_fallback(301u, tiny),
        1.59327514414367217570110220213512475312e31L, 2e-12L);
    expect_relative(math::assoc_legendre_fallback(700u, 700u, 1.0f - 0x1p-19f),
        8.80763167988997025406382404325188401679e1L, 3e-6L);
}

TEST(SimdMathPolynomialScaling, OrdinaryFiniteValuesAndLegendrePhaseRemainUnchanged) {
    EXPECT_EQ(math::hermite_fallback(3u, 2.0), 40.0);
    EXPECT_EQ(math::laguerre_fallback(2u, 2.0), -1.0);
    EXPECT_EQ(math::assoc_laguerre_fallback(2u, 3u, 2.0), 2.0);
    EXPECT_EQ(math::assoc_legendre_fallback(3u, 1u, 0.0), -1.5);
    EXPECT_EQ(math::legendre_fallback(2u, 0.25), -0.40625);
    expect_relative(math::hermite_fallback(31u, 1.25),
        1.53433102636664801192299125395948067307e21L, 2e-12L);
    expect_relative(math::assoc_legendre_fallback(37u, 13u, 0.25),
        -2.58801088207341828942623057513801711526e19L, 2e-12L);
    EXPECT_EQ(math::assoc_legendre_fallback(100u, 99u, 1.0), 0.0);
    EXPECT_EQ(math::assoc_legendre_fallback(100u, 99u, -1.0), 0.0);
}

TEST(SimdMathPolynomialScaling, ExistingDomainDegreeNaNAndZeroOrderPrioritiesRemain) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(std::isnan(math::hermite_fallback(0u, nan)));
    EXPECT_TRUE(std::isnan(math::laguerre_fallback(0u, nan)));
    EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(0u, 0u, nan)));
    EXPECT_TRUE(std::isnan(math::assoc_legendre_fallback(0u, 0u, nan)));
    for (const unsigned degree : {1024u, std::numeric_limits<unsigned>::max()}) {
        EXPECT_TRUE(std::isnan(math::hermite_fallback(degree, 0.0)));
        EXPECT_TRUE(std::isnan(math::laguerre_fallback(degree, 0.0)));
        EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(degree, 1u, 0.0)));
        EXPECT_TRUE(std::isnan(math::assoc_legendre_fallback(degree, 1u, 0.0)));
    }
    EXPECT_TRUE(std::isnan(math::assoc_laguerre_fallback(0u, 1024u, 0.0)));
    EXPECT_TRUE(std::isnan(math::assoc_legendre_fallback(3u, 4u, 1.1)));
    EXPECT_EQ(math::assoc_legendre_fallback(3u, 4u, 0.0), 0.0);
    EXPECT_EQ(math::assoc_legendre_fallback(1024u, 1025u, 0.0), 0.0);
    EXPECT_EQ(math::hermite_fallback(0u, -inf), 1.0);
    EXPECT_EQ(math::laguerre_fallback(0u, inf), 1.0);
    EXPECT_EQ(math::assoc_laguerre_fallback(0u, 7u, inf), 1.0);
}

#ifndef FORGE_SM06_POLYNOMIAL_PRIVATE_KERNEL_PROBE
TEST(SimdMathPolynomialScaling, PublicFloatFallbackPreservesFiniteAndZeroLanes) {
#ifdef __cpp_lib_math_special_functions
    GTEST_SKIP() << "Native scalar special functions selected";
#else
    using vector = std::simd::vec<float, 2>;
    const vector small([](auto index) {
        return decltype(index)::value == 0 ? 0.0f : 1e-7f;
    });
    const auto hermite = std::simd::hermite(51u, small);
    const auto legendre = std::simd::assoc_legendre(30u, 29u, small);
    EXPECT_EQ(hermite[0], 0.0f);
    EXPECT_EQ(legendre[0], 0.0f);
    expect_relative(static_cast<float>(hermite[1]),
        -1.99999712089586339869978623277427191762e34L, 3e-6L);
    expect_relative(static_cast<float>(legendre[1]),
        2.92156067128853553533482828922983151255e33L, 3e-6L);
    const auto laguerre = std::simd::assoc_laguerre(50u, 10u, vector(200.0f));
    for (std::simd::simd_size_type i = 0; i != vector::size; ++i) {
        expect_relative(static_cast<float>(laguerre[i]),
            3.32888592429968127868390631133283872650e38L, 3e-6L);
    }
#endif
}

TEST(SimdMathPolynomialScaling, PublicDoubleFallbackPreservesOversizedIntermediates) {
#ifdef __cpp_lib_math_special_functions
    GTEST_SKIP() << "Native scalar special functions selected";
#else
    using vector = std::simd::vec<double, 2>;
    const auto laguerre = std::simd::laguerre(1000u, vector(1420.0));
    const auto hermite = std::simd::hermite(301u, vector(1e-100));
    const auto legendre = std::simd::assoc_legendre(172u, 171u, vector(1e-100));
    for (std::simd::simd_size_type i = 0; i != vector::size; ++i) {
        expect_relative(static_cast<double>(laguerre[i]),
            -3.97219102141256760897412813903336452253e306L, 2e-12L);
        expect_relative(static_cast<double>(hermite[i]),
            3.22482479313209380254805406277716615401e254L, 2e-12L);
        expect_relative(static_cast<double>(legendre[i]),
            5.49301804843003796986449807797434860386e261L, 2e-12L);
    }
#endif
}
#endif

} // namespace

#ifdef FORGE_SM06_POLYNOMIAL_PRIVATE_KERNEL_PROBE
int main() {
    const struct {
        const char* name;
        void (*run)();
    } cases[] = {
        {"reported float", SimdMathPolynomialScaling_ReportedFloatResultsRemainRepresentable},
        {"finite float", SimdMathPolynomialScaling_FloatNonzeroResultsSurviveOversizedIntermediateTerms},
        {"finite double", SimdMathPolynomialScaling_DoubleResultsDoNotRequireAWiderLongDoubleRange},
        {"zero parity", SimdMathPolynomialScaling_ZeroParitySurvivesUnrepresentableNeighbors},
        {"signed overflow", SimdMathPolynomialScaling_GenuineOverflowHasThePolynomialSignInsteadOfNaN},
        {"subnormals", SimdMathPolynomialScaling_SubnormalArgumentsAndResultsAreNotDiscarded},
        {"finite phase", SimdMathPolynomialScaling_OrdinaryFiniteValuesAndLegendrePhaseRemainUnchanged},
        {"guard priorities", SimdMathPolynomialScaling_ExistingDomainDegreeNaNAndZeroOrderPrioritiesRemain},
    };
    std::printf("SM06 polynomial kernels: %d work bits\n",
                std::numeric_limits<long double>::digits);
#ifdef _MSC_FULL_VER
    std::printf("MSVC version %d\n", _MSC_FULL_VER);
#endif
    unsigned completed = 0u;
    for (const auto& test : cases) {
        private_case = test.name;
        const unsigned failures_before = private_failures;
        test.run();
        ++completed;
        std::printf("%s: %s\n", test.name,
                    private_failures == failures_before ? "PASS" : "FAIL");
    }
    std::printf("SM06 polynomial result: %u/8 cases, %u/105 checks, %u failures, no skips\n",
                completed, private_checked, private_failures);
    return completed == 8u && private_checked == 105u && private_failures == 0u ? 0 : 1;
}

#undef TEST
#undef EXPECT_TRUE
#undef EXPECT_EQ
#undef EXPECT_LE
#undef ASSERT_TRUE
#undef ASSERT_NE
#endif

#ifndef FORGE_SM07_PRIVATE_KERNEL_PROBE
#include "simd_test_common.hpp"

#include <gtest/gtest.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <type_traits>

#ifdef FORGE_SM07_PRIVATE_KERNEL_PROBE
// Standalone MSVC mode: private kernels only, without the SIMD frontend.
namespace std::simd {
#include "common.hpp"
#include "bessel.hpp"
}
static_assert(std::numeric_limits<long double>::digits == 53,
              "SM07 private-kernel probe requires 53-bit working precision");
#endif

namespace {

namespace math = std::simd::detail::special_math;

static_assert(std::is_same_v<decltype(math::cyl_bessel_jy_fallback(0.0f, 1.0f)),
                             math::cyl_bessel_hankel_result<float>>);
static_assert(std::is_same_v<decltype(math::cyl_bessel_jy_fallback(0.0, 1.0)),
                             math::cyl_bessel_hankel_result<double>>);

enum class component { j, y };

template<class T>
struct reflection_reference {
    T order;
    T argument;
    long double finite_value;
    component finite_component;
};

// mpmath 1.3.0, 100 decimal digits, from exact binary input ratios;
// independently checked at 160 digits. The other reflected component overflows T.
constexpr reflection_reference<float> float_references[] = {
    {-0x1.e028f6p+4f, 1.0f,
     9.97310195072191262242536868676746363e37L, component::j},
    {-0x1.e80002p+4f, 1.0f,
     1.40870261760880742141341339373048045e35L, component::y},
};

constexpr reflection_reference<double> double_references[] = {
    {-0x1.8000008637bd0p+5, 0x1.4f8b588e368f1p-17,
     7.27971403065284806772690621287627331e307L, component::j},
    {-0x1.84020c49ba5e3p+5, 0x1.d5c31593e5fb7p-17,
     1.86955271443861010254619716459567838e307L, component::y},
};

template<class T>
long double relative_tolerance() {
    return std::numeric_limits<T>::digits <= 24
        ? 8.0L * std::numeric_limits<T>::epsilon() : 3e-12L;
}

#ifdef FORGE_SM07_PRIVATE_KERNEL_PROBE
template<class T, std::size_t N>
unsigned check_private_references(const char* group,
    const reflection_reference<T> (&references)[N], unsigned& checked) {
    static_assert(N == 2u, "Two mandatory references per input type");
    unsigned failures = 0u;
    long double maximum_relative_error = 0.0L;
    for (std::size_t index = 0u; index < N; ++index) {
        const auto& reference = references[index];
        const auto positive = math::cyl_bessel_jy_fallback(
            -reference.order, reference.argument);
        const auto reflected = math::cyl_bessel_jy_fallback(
            reference.order, reference.argument);
        const T finite = reference.finite_component == component::j
            ? reflected.j : reflected.y;
        const T overflow = reference.finite_component == component::j
            ? reflected.y : reflected.j;
        const long double error = std::abs(
            static_cast<long double>(finite) - reference.finite_value);
        const bool passed = positive.converged && std::isinf(positive.y) &&
            std::signbit(positive.y) && reflected.converged &&
            std::isfinite(finite) &&
            error <= relative_tolerance<T>() * std::abs(reference.finite_value) &&
            std::isinf(overflow) &&
            std::signbit(overflow) == (reference.finite_component == component::j);
        ++checked;
        if (!passed) {
            std::printf("FAIL: %s reference %zu\n", group, index);
            ++failures;
        }
        if (std::isfinite(finite)) {
            maximum_relative_error = std::max(maximum_relative_error,
                error / std::abs(reference.finite_value));
        }
    }
    std::printf("%s: %zu mandatory references, max relative error %.6Le\n",
                group, N, maximum_relative_error);
    return failures;
}
#else
template<class T, std::size_t N>
void check_overflow_references(const reflection_reference<T> (&references)[N]) {
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.order);
        const auto positive = math::cyl_bessel_jy_fallback(
            -reference.order, reference.argument);
        ASSERT_TRUE(positive.converged);
        ASSERT_TRUE(std::isinf(positive.y));
        EXPECT_TRUE(std::signbit(positive.y));

        const auto reflected = math::cyl_bessel_jy_fallback(
            reference.order, reference.argument);
        ASSERT_TRUE(reflected.converged);
        const T finite = reference.finite_component == component::j
            ? reflected.j : reflected.y;
        const T overflow = reference.finite_component == component::j
            ? reflected.y : reflected.j;
        ASSERT_TRUE(std::isfinite(finite));
        EXPECT_NEAR(static_cast<long double>(finite), reference.finite_value,
                    relative_tolerance<T>() * std::abs(reference.finite_value));
        EXPECT_TRUE(std::isinf(overflow));
        EXPECT_EQ(std::signbit(overflow), reference.finite_component == component::j);
    }
}

TEST(SimdMathReflection, FloatCombinationPrecedesNarrowing) {
    check_overflow_references(float_references);
}

TEST(SimdMathReflection, DoubleCombinationDoesNotRequireWiderWorkingExponentRange) {
    check_overflow_references(double_references);
}

template<class T>
void check_moderate_references() {
    constexpr std::array<std::array<long double, 4>, 3> references{{
        {{-0.25L, 1.0L, 0.66938481726157445152326532562806040576L,
           0.39443093639096740087101957050463495787L}},
        {{-3.25L, 2.0L, -1.0263568263661548327443691192029408613L,
           0.89219825394668514981993830326770805841L}},
        {{-3.75L, 4.0L, -0.068546071827240339292812767739767102079L,
           -0.52715321037675159660173828818882088453L}},
    }};
    for (const auto& reference : references) {
        SCOPED_TRACE(reference[0]);
        const auto result = math::cyl_bessel_jy_fallback(
            static_cast<T>(reference[0]), static_cast<T>(reference[1]));
        ASSERT_TRUE(result.converged);
        EXPECT_NEAR(static_cast<long double>(result.j), reference[2],
                    relative_tolerance<T>() * std::max(1.0L, std::abs(reference[2])));
        EXPECT_NEAR(static_cast<long double>(result.y), reference[3],
                    relative_tolerance<T>() * std::max(1.0L, std::abs(reference[3])));
    }
}

TEST(SimdMathReflection, FloatAndDoubleKeepNormalRangeValues) {
    check_moderate_references<float>();
    check_moderate_references<double>();
}

template<class T>
void check_integer_zero_coefficients() {
    for (T order : {T{20}, T{21}}) {
        const T argument = static_cast<T>(0.01L);
        const auto positive = math::cyl_bessel_jy_fallback(order, argument);
        const auto negative = math::cyl_bessel_jy_fallback(-order, argument);
        ASSERT_TRUE(positive.converged);
        ASSERT_TRUE(negative.converged);
        const T parity = order == T{20} ? T{1} : T{-1};
        EXPECT_FALSE(std::isnan(negative.j));
        EXPECT_FALSE(std::isnan(negative.y));
        EXPECT_EQ(negative.j, parity * positive.j);
        EXPECT_EQ(negative.y, parity * positive.y);
    }
}

TEST(SimdMathReflection, IntegerZeroCoefficientsDoNotMultiplyInfinity) {
    check_integer_zero_coefficients<float>();
    check_integer_zero_coefficients<double>();
    const auto large_ratio = math::cyl_bessel_jy_fallback(-2.0, 1e-308);
    ASSERT_TRUE(large_ratio.converged);
    EXPECT_EQ(large_ratio.j, 0.0);
    EXPECT_FALSE(std::signbit(large_ratio.j));
    EXPECT_TRUE(std::isinf(large_ratio.y));
    EXPECT_TRUE(std::signbit(large_ratio.y));
}

TEST(SimdMathReflection, PositiveTinyArgumentsOverflowToNegativeInfinity) {
    const auto check = []<class T>(T tiny) {
        SCOPED_TRACE(std::numeric_limits<T>::digits);
        for (const T order : {T{20}, T{21}}) {
            SCOPED_TRACE(order);
            const T value = math::cyl_bessel_y_fallback(order, tiny);
            EXPECT_FALSE(std::isnan(value));
            EXPECT_TRUE(std::isinf(value));
            EXPECT_TRUE(std::signbit(value));
        }
        constexpr std::array<long double, 2> finite_references{
            -0.78121282130028871654715000004796482055L,
            -1.65068260681625439107722676611944480393L};
        for (std::size_t i = 0; i != finite_references.size(); ++i) {
            const T value = math::cyl_bessel_y_fallback(static_cast<T>(i + 1u), T{1});
            ASSERT_TRUE(std::isfinite(value));
            EXPECT_NEAR(static_cast<long double>(value), finite_references[i],
                        relative_tolerance<T>() * std::abs(finite_references[i]));
        }
    };
    check(1e-30f);
    check(1e-300);
    check(1e-300L);
}

TEST(SimdMathReflection, GenuinelyOverflowingReflectionsRemainSignedInfinities) {
    const auto float_result = math::cyl_bessel_jy_fallback(-30.25f, 1.0f);
    ASSERT_TRUE(float_result.converged);
    EXPECT_TRUE(std::isinf(float_result.j));
    EXPECT_FALSE(std::signbit(float_result.j));
    EXPECT_TRUE(std::isinf(float_result.y));
    EXPECT_TRUE(std::signbit(float_result.y));
    // Exact-input 100/160-digit oracle: J < -DBL_MAX, Y > DBL_MAX.
    const auto double_result = math::cyl_bessel_jy_fallback(-49.25, 1e-5);
    ASSERT_TRUE(double_result.converged);
    EXPECT_TRUE(std::isinf(double_result.j));
    EXPECT_TRUE(std::signbit(double_result.j));
    EXPECT_TRUE(std::isinf(double_result.y));
    EXPECT_FALSE(std::signbit(double_result.y));
}

template<class T>
void check_half_integer_identity() {
    for (T x : {static_cast<T>(0.125L), T{1}}) {
        const long double argument = static_cast<long double>(x);
        const long double scale = std::sqrt(2.0L / (math::pi_v<long double> * argument));
        const auto result = math::cyl_bessel_jy_fallback(T{-0.5}, x);
        ASSERT_TRUE(result.converged);
        EXPECT_NEAR(static_cast<long double>(result.j), scale * std::cos(argument),
                    relative_tolerance<T>() * scale);
        EXPECT_NEAR(static_cast<long double>(result.y), scale * std::sin(argument),
                    relative_tolerance<T>() * scale);
    }
}

TEST(SimdMathReflection, NegativeHalfIntegerIdentityIsUnchanged) {
    check_half_integer_identity<float>();
    check_half_integer_identity<double>();
}

template<class T>
void check_domains() {
    const auto half_zero = math::cyl_bessel_jy_fallback(T{-0.5}, T{});
    ASSERT_TRUE(half_zero.converged);
    EXPECT_TRUE(std::isinf(half_zero.j));
    EXPECT_FALSE(std::signbit(half_zero.j));
    EXPECT_EQ(half_zero.y, T{});
    EXPECT_FALSE(std::signbit(half_zero.y));
    const auto integer_zero = math::cyl_bessel_jy_fallback(T{-1}, T{});
    ASSERT_TRUE(integer_zero.converged);
    EXPECT_EQ(integer_zero.j, T{});
    EXPECT_TRUE(std::signbit(integer_zero.j));
    EXPECT_TRUE(std::isinf(integer_zero.y));
    EXPECT_FALSE(std::signbit(integer_zero.y));
    const auto infinite_argument = math::cyl_bessel_jy_fallback(
        T{-0.25}, std::numeric_limits<T>::infinity());
    ASSERT_TRUE(infinite_argument.converged);
    EXPECT_EQ(infinite_argument.j, T{});
    EXPECT_EQ(infinite_argument.y, T{});
    const auto integer_infinite = math::cyl_bessel_jy_fallback(
        T{-1}, std::numeric_limits<T>::infinity());
    ASSERT_TRUE(integer_infinite.converged);
    EXPECT_EQ(integer_infinite.j, T{});
    EXPECT_TRUE(std::signbit(integer_infinite.j));
    EXPECT_EQ(integer_infinite.y, T{});
    const auto invalid_argument = math::cyl_bessel_jy_fallback(T{-0.25}, T{-1});
    EXPECT_FALSE(invalid_argument.converged);
    EXPECT_TRUE(std::isnan(invalid_argument.j));
    EXPECT_TRUE(std::isnan(invalid_argument.y));
    for (T invalid_order : {std::numeric_limits<T>::quiet_NaN(),
                            -std::numeric_limits<T>::infinity()}) {
        const auto invalid = math::cyl_bessel_jy_fallback(invalid_order, T{1});
        EXPECT_FALSE(invalid.converged);
        EXPECT_TRUE(std::isnan(invalid.j));
        EXPECT_TRUE(std::isnan(invalid.y));
    }
}

TEST(SimdMathReflection, DomainsAndZeroLimitsRemainUnchanged) {
    check_domains<float>();
    check_domains<double>();
}

#ifndef __cpp_lib_math_special_functions
template<class T, std::size_t N>
void check_public(const reflection_reference<T> (&references)[N]) {
    using vector = std::simd::vec<T, 2>;
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.order);
        const vector arguments(reference.argument);
        const auto values = reference.finite_component == component::j
            ? std::simd::cyl_bessel_j(reference.order, arguments)
            : std::simd::cyl_neumann(reference.order, arguments);
        for (std::simd::simd_size_type lane = 0; lane < vector::size; ++lane) {
            ASSERT_TRUE(std::isfinite(values[lane]));
            EXPECT_NEAR(static_cast<long double>(values[lane]), reference.finite_value,
                        relative_tolerance<T>() * std::abs(reference.finite_value));
        }
    }
}

TEST(SimdMathReflection, PublicFloatFallbackReflectsBeforeNarrowing) {
    check_public(float_references);
}

TEST(SimdMathReflection, PublicDoubleFallbackReflectsBeforeNarrowing) {
    check_public(double_references);
}
#endif
#endif

} // namespace

#ifdef FORGE_SM07_PRIVATE_KERNEL_PROBE
int main() {
    std::printf("SM07 private kernels: work digits %d\n",
                std::numeric_limits<long double>::digits);
#ifdef _MSC_FULL_VER
    std::printf("MSVC version %d\n", _MSC_FULL_VER);
#endif
    unsigned checked = 0u;
    unsigned failures = check_private_references("float", float_references, checked);
    failures += check_private_references("double", double_references, checked);
    std::printf("SM07 private-kernel result: %u/4 checked, %u failures, no skips\n",
                checked, failures);
    return checked == 4u && failures == 0u ? 0 : 1;
}
#endif

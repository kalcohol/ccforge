#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

namespace math = std::simd::detail::special_math;

template<class Fun>
struct counted_function {
    Fun fun;
    std::size_t calls = 0u;

    long double operator()(long double x) {
        ++calls;
        return fun(x);
    }
};

namespace cache_probe {
using namespace math;

// Old headers select the baseline, so before/head distinguishes work rather
// than merely failing to compile when the cached helpers are absent.
template<class Fun>
auto integral(Fun& fun, long double a, long double b, long double tolerance,
              unsigned depth = 18u) -> math::checked_integral_result {
    if constexpr (requires {
        cyl_bessel_cached_simpson_integral(fun, a, b, tolerance, depth);
    }) {
        return cyl_bessel_cached_simpson_integral(fun, a, b, tolerance, depth);
    } else {
        return math::checked_simpson_integral(fun, a, b, tolerance, depth);
    }
}

template<class Fun>
auto segmented(Fun& fun, long double a, long double b,
               unsigned segments, long double tolerance) -> math::checked_integral_result {
    if constexpr (requires {
        cyl_bessel_segmented_cached_simpson_integral(fun, a, b, segments, tolerance);
    }) {
        return cyl_bessel_segmented_cached_simpson_integral(fun, a, b, segments, tolerance);
    } else {
        return math::segmented_checked_simpson_integral(fun, a, b, segments, tolerance);
    }
}
} // namespace cache_probe

void expect_same_result(const math::checked_integral_result& cached,
                        const math::checked_integral_result& baseline) {
    EXPECT_EQ(cached.value, baseline.value);
    EXPECT_EQ(cached.error, baseline.error);
    EXPECT_EQ(cached.converged, baseline.converged);
}

template<class Fun>
void check_finite_phase(Fun fun) {
    counted_function<Fun> baseline{fun};
    counted_function<Fun> optimized{fun};
    const auto reference = math::segmented_checked_simpson_integral(
        baseline, 0.0L, math::pi_v<long double>, 29u, 2e-14L);
    const auto cached = cache_probe::segmented(
        optimized, 0.0L, math::pi_v<long double>, 29u, 2e-14L);
    ASSERT_TRUE(reference.converged);
    expect_same_result(cached, reference);
    EXPECT_LE(optimized.calls * 2u, baseline.calls);
}

TEST(SimdMathQuadrature, FiniteJAndYPhaseReuseSamples) {
    check_finite_phase([](long double theta) {
        return std::cos(14.0L * std::sin(theta) - 0.25L * theta);
    });
    check_finite_phase([](long double theta) {
        return std::sin(14.0L * std::sin(theta) - 0.25L * theta);
    });
}

template<class Fun>
void check_decaying_tail(Fun fun) {
    counted_function<Fun> baseline{fun};
    counted_function<Fun> optimized{fun};
    const auto reference = math::checked_simpson_integral(
        baseline, 0.0L, 4.0L, 2e-14L);
    const auto cached = cache_probe::integral(
        optimized, 0.0L, 4.0L, 2e-14L);
    ASSERT_TRUE(reference.converged);
    expect_same_result(cached, reference);
    EXPECT_LE(optimized.calls * 2u, baseline.calls);
}

TEST(SimdMathQuadrature, DecayingJAndYTailReuseSamples) {
    check_decaying_tail([](long double t) {
        return std::exp(-14.0L * std::sinh(t) - 0.25L * t);
    });
    check_decaying_tail([](long double t) {
        const long double decay = -14.0L * std::sinh(t);
        return std::exp(decay + 0.25L * t) +
            std::cos(math::pi_v<long double> * 0.25L) *
                std::exp(decay - 0.25L * t);
    });
}

TEST(SimdMathQuadrature, ExhaustedDepthRetainsFailureAndError) {
    const auto quartic = [](long double x) { return x * x * x * x; };
    for (unsigned depth : {0u, 1u}) {
        SCOPED_TRACE(depth);
        counted_function<decltype(quartic)> baseline{quartic};
        counted_function<decltype(quartic)> optimized{quartic};
        const auto reference = math::checked_simpson_integral(
            baseline, 0.0L, 1.0L, 1e-30L, depth);
        const auto cached = cache_probe::integral(
            optimized, 0.0L, 1.0L, 1e-30L, depth);
        EXPECT_FALSE(cached.converged);
        EXPECT_GT(cached.error, 0.0L);
        expect_same_result(cached, reference);
        EXPECT_EQ(optimized.calls, depth == 0u ? 5u : 9u);
        EXPECT_EQ(baseline.calls, depth == 0u ? 9u : 21u);
    }
}

TEST(SimdMathQuadrature, EmptyIntervalsDoNotEvaluateIntegrands) {
    const auto identity = [](long double x) { return x; };
    counted_function<decltype(identity)> fun{identity};
    const auto scalar = cache_probe::integral(fun, 1.0L, 1.0L, 1e-14L);
    const auto segmented = cache_probe::segmented(fun, 1.0L, 1.0L, 8u, 1e-14L);
    const auto no_segments = cache_probe::segmented(fun, 0.0L, 1.0L, 0u, 1e-14L);
    EXPECT_EQ(fun.calls, 0u);
    EXPECT_TRUE(scalar.converged);
    EXPECT_TRUE(segmented.converged);
    EXPECT_TRUE(no_segments.converged);
    EXPECT_EQ(scalar.value, 0.0L);
    EXPECT_EQ(scalar.error, 0.0L);
    EXPECT_EQ(segmented.value, 0.0L);
    EXPECT_EQ(segmented.error, 0.0L);
    EXPECT_EQ(no_segments.value, 0.0L);
    EXPECT_EQ(no_segments.error, 0.0L);
}

struct quadrature_reference {
    long double order;
    long double argument;
    long double j;
    long double y;
};

// Exact binary inputs, mpmath 1.3.0 at 100 digits, checked independently at 160.
constexpr quadrature_reference references[] = {
    {-0.25L, 12.0L, 0.13075993131132577344154896716748912061L,
     -0.18952395515598502210858939522179880206L},
    {-0.25L, 14.0L, 0.10897780412579270976147874985307651376L,
     0.18323511181360099159401894951217881695L},
    {-0.25L, 16.0L, -0.19830467750799517213399707345650701226L,
     0.021201678121025208579757241201196909129L},
    {0.25L, 12.0L, -0.041552439750366528538882868993608279373L,
     -0.22647490802581776449396538754550046873L},
    {0.25L, 14.0L, 0.20662573441103986732360724378865912214L,
     0.052507845818705184029580704728342697115L},
    {0.25L, 16.0L, -0.12523073183500343037212935006757142308L,
     0.15521443257882619882274239553023317054L},
    {1.25L, 12.0L, -0.22921564342703801390609257834183789386L,
     0.027436558260608862623194153032050402382L},
    {1.25L, 14.0L, 0.063595054864926107686936681402863428931L,
     -0.20391265235095450743399386145430713014L},
    {1.25L, 16.0L, 0.14940357823186386350230041768895512883L,
     0.13254848797289518848942483851919562922L},
};

template<class T>
void check_quadrature_references() {
    const long double target = math::cyl_bessel_target_tolerance<T>();
    for (const auto& reference : references) {
        SCOPED_TRACE(reference.order);
        SCOPED_TRACE(reference.argument);
        const auto result = math::cyl_bessel_reduced_integral_pair<T>(
            reference.order, reference.argument);
        ASSERT_TRUE(result.converged);
        EXPECT_LE(std::abs(result.j - reference.j),
                  target * std::max(1.0L, std::abs(reference.j)));
        EXPECT_LE(std::abs(result.y - reference.y),
                  target * std::max(1.0L, std::abs(reference.y)));
    }
}

TEST(SimdMathQuadrature, FloatDoubleAndLongDoubleRetainReferenceValues) {
    check_quadrature_references<float>();
    check_quadrature_references<double>();
    check_quadrature_references<long double>();
}

#ifndef __cpp_lib_math_special_functions
template<class T>
void check_public_quadrature_references() {
    using vector = std::simd::vec<T, 2>;
    const long double target = math::cyl_bessel_target_tolerance<T>();
    for (const auto& reference : references) {
        if (reference.order < 0.0L) {
            continue;
        }
        SCOPED_TRACE(reference.order);
        SCOPED_TRACE(reference.argument);
        const vector arguments(static_cast<T>(reference.argument));
        const auto j = std::simd::cyl_bessel_j(static_cast<T>(reference.order), arguments);
        const auto y = std::simd::cyl_neumann(static_cast<T>(reference.order), arguments);
        for (std::simd::simd_size_type i = 0; i < vector::size; ++i) {
            EXPECT_LE(std::abs(static_cast<long double>(j[i]) - reference.j),
                      target * std::max(1.0L, std::abs(reference.j)));
            EXPECT_LE(std::abs(static_cast<long double>(y[i]) - reference.y),
                      target * std::max(1.0L, std::abs(reference.y)));
        }
    }
}

TEST(SimdMathQuadrature, PublicFloatAndDoubleFallbackRetainReferenceValues) {
    check_public_quadrature_references<float>();
    check_public_quadrature_references<double>();
}
#endif

} // namespace

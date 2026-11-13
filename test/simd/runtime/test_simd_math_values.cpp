#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

namespace {
template<class T> void same_value(T actual, T expected) {
    if (std::isnan(expected)) EXPECT_TRUE(std::isnan(actual));
    else {
        EXPECT_EQ(actual, expected);
        if (expected == T(0)) EXPECT_EQ(std::signbit(actual), std::signbit(expected));
    }
}
template<class T> void special_values() {
    using V = std::simd::vec<T, 9>;
    const T inf = std::numeric_limits<T>::infinity();
    const T nan = std::numeric_limits<T>::quiet_NaN();
    const T tiny = std::numeric_limits<T>::denorm_min();
    const T lanes[]{T(0), -T(0), inf, -inf, nan, tiny, -tiny, T(4), T(-1)};
    const V x([&](auto i) { return lanes[int(i)]; });
    const auto roots = std::simd::sqrt(x);
    const auto sines = std::simd::sin(x);
    const auto exps = std::simd::exp(x);
    const auto fused = std::simd::fma(x, V(T(1)), V(-T(0)));
    for (int i = 0; i < 9; ++i) {
        same_value(T(roots[i]), std::sqrt(lanes[i]));
        same_value(T(sines[i]), std::sin(lanes[i]));
        same_value(T(exps[i]), std::exp(lanes[i]));
        same_value(T(fused[i]), std::fma(lanes[i], T(1), -T(0)));
    }
    // A fused operation must retain the residual lost by separate multiply/add.
    const T step = std::numeric_limits<T>::epsilon();
    same_value(T(std::simd::fma(V(T(1) + step), V(T(1) - step), V(T(-1)))[0]),
        std::fma(T(1) + step, T(1) - step, T(-1)));
}
TEST(SimdMathValues, FloatSpecialValues) { special_values<float>(); }
TEST(SimdMathValues, DoubleSpecialValues) { special_values<double>(); }
} // namespace

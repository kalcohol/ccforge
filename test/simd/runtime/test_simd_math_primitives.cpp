#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

TEST(SimdMathPrimitives, LongDoubleRetainsDeclaredPrecision) {
    volatile long double source = 2.0L;
    const long double input = source;
    constexpr long double sqrt_two = 1.41421356237309504880168872420969807857L;
    constexpr long double log_two = 0.693147180559945309417232121458176568076L;
    constexpr long double exp_one = 2.718281828459045235360287471352662497757L;
    constexpr long double epsilon = std::numeric_limits<long double>::epsilon();

    EXPECT_LE(std::abs(std::sqrt(input) - sqrt_two), 8.0L * epsilon * sqrt_two);
    EXPECT_LE(std::abs(std::log(input) - log_two), 8.0L * epsilon);
    EXPECT_LE(std::abs(std::exp(input / 2.0L) - exp_one), 8.0L * epsilon * exp_one);
}

TEST(SimdMathPrimitives, LongDoubleSquareRootRetainsNormalRange) {
    volatile long double source = std::numeric_limits<long double>::min();
    const long double input = source;
    const long double result = std::sqrt(input);
    ASSERT_GT(result, 0.0L);
    ASSERT_TRUE(std::isfinite(result));
    EXPECT_LE(std::abs((result * result) / input - 1.0L),
              8.0L * std::numeric_limits<long double>::epsilon());
}

} // namespace

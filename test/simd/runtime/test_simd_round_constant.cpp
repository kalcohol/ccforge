#include <simd>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

template<class T>
constexpr bool preserves_special_values() {
    using vector = std::simd::vec<T, 5>;
    constexpr T infinity = std::numeric_limits<T>::infinity();
    constexpr T nan = std::numeric_limits<T>::quiet_NaN();
    const vector input([=](auto lane) {
        if constexpr (decltype(lane)::value == 0) return infinity;
        else if constexpr (decltype(lane)::value == 1) return -infinity;
        else if constexpr (decltype(lane)::value == 2) return nan;
        else if constexpr (decltype(lane)::value == 3) return T{0};
        else return -T{0};
    });
    const auto rounded = std::simd::round(input);
    using bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    return rounded[0] == infinity && rounded[1] == -infinity &&
        rounded[2] != rounded[2] &&
        std::bit_cast<bits>(rounded[3]) == std::bit_cast<bits>(T{0}) &&
        std::bit_cast<bits>(rounded[4]) == std::bit_cast<bits>(-T{0});
}

template<class T>
constexpr bool rounds_finite_values() {
    using vector = std::simd::vec<T, 6>;
    const vector input([](auto lane) {
        constexpr T values[]{T{1.5}, T{-1.5}, T{0.5}, T{-0.5}, T{0.25}, T{-0.25}};
        return values[lane];
    });
    const auto rounded = std::simd::round(input);
    using bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    return rounded[0] == T{2} && rounded[1] == T{-2} &&
        rounded[2] == T{1} && rounded[3] == T{-1} &&
        std::bit_cast<bits>(rounded[4]) == std::bit_cast<bits>(T{0}) &&
        std::bit_cast<bits>(rounded[5]) == std::bit_cast<bits>(-T{0});
}

static_assert(preserves_special_values<float>());
static_assert(preserves_special_values<double>());
static_assert(rounds_finite_values<float>());
static_assert(rounds_finite_values<double>());

TEST(SimdRoundConstant, FloatSpecialAndFiniteValuesAreConstantEvaluable) {
    EXPECT_TRUE(preserves_special_values<float>());
    EXPECT_TRUE(rounds_finite_values<float>());
}

TEST(SimdRoundConstant, DoubleSpecialAndFiniteValuesAreConstantEvaluable) {
    EXPECT_TRUE(preserves_special_values<double>());
    EXPECT_TRUE(rounds_finite_values<double>());
}

TEST(SimdRoundConstant, RuntimeRoundKeepsScalarSemantics) {
    using vector = std::simd::vec<double, 4>;
    const vector values([](auto lane) {
        const double inputs[]{std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(), -0.25};
        return inputs[lane];
    });
    const auto rounded = std::simd::round(values);
    for (std::simd::simd_size_type lane = 0; lane < vector::size; ++lane) {
        if (std::isnan(values[lane])) {
            EXPECT_TRUE(std::isnan(rounded[lane]));
        } else {
            EXPECT_EQ(rounded[lane], std::round(values[lane]));
            EXPECT_EQ(std::signbit(rounded[lane]), std::signbit(std::round(values[lane])));
        }
    }
}

} // namespace

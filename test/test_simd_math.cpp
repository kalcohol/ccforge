#include <simd>

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

namespace {

using int4 = std::simd::vec<int, 4>;
using mask4 = std::simd::mask<int, 4>;

TEST(SimdMathTest, ReduceUsesProvidedBinaryOperation) {
    int4 values{2, 3, 4, 5};

    EXPECT_EQ(std::simd::reduce(values, std::multiplies<>{}), 120);
}

TEST(SimdMathTest, MaskedReduceStartsFromIdentityElement) {
    int4 values{2, 3, 4, 5};
    mask4 selected{false, true, true, false};

    EXPECT_EQ(std::simd::reduce(values, selected), 7);
    EXPECT_EQ(std::simd::reduce(values, selected, std::plus<>{}, 10), 17);
}

TEST(SimdMathTest, ReduceMinAndReduceMaxHonorMaskSelection) {
    int4 values{8, 3, 6, 1};
    mask4 selected{false, true, true, false};

    EXPECT_EQ(std::simd::reduce_min(values), 1);
    EXPECT_EQ(std::simd::reduce_max(values), 8);
    EXPECT_EQ(std::simd::reduce_min(values, selected), 3);
    EXPECT_EQ(std::simd::reduce_max(values, selected), 6);
}

TEST(SimdMathTest, BasicMathFunctionsApplyPerLane) {
    const auto abs_value = std::simd::abs(std::simd::vec<float, 4>{-1.0f, -4.0f, -9.0f, -16.0f});
    const auto sqrt_value = std::simd::sqrt(std::simd::vec<float, 4>{1.0f, 4.0f, 9.0f, 16.0f});
    const auto min_value = std::simd::min(std::simd::vec<float, 4>{1.0f, 5.0f, 3.0f, 7.0f},
                                          std::simd::vec<float, 4>{2.0f, 4.0f, 6.0f, 1.0f});
    const auto max_value = std::simd::max(std::simd::vec<float, 4>{1.0f, 5.0f, 3.0f, 7.0f},
                                          std::simd::vec<float, 4>{2.0f, 4.0f, 6.0f, 1.0f});

    EXPECT_FLOAT_EQ(abs_value[0], 1.0f);
    EXPECT_FLOAT_EQ(abs_value[3], 16.0f);
    EXPECT_FLOAT_EQ(sqrt_value[1], 2.0f);
    EXPECT_FLOAT_EQ(sqrt_value[3], 4.0f);
    EXPECT_FLOAT_EQ(min_value[1], 4.0f);
    EXPECT_FLOAT_EQ(max_value[2], 6.0f);
}

TEST(SimdMathTest, RoundingAndTranscendentalFunctionsRemainComposable) {
    const auto rounded = std::simd::floor(std::simd::vec<float, 4>{1.9f, 2.1f, 3.8f, 4.0f});
    const auto ceiled = std::simd::ceil(std::simd::vec<float, 4>{1.2f, 2.0f, 3.1f, 4.8f});
    const auto rounded_nearest = std::simd::round(std::simd::vec<float, 4>{1.2f, 2.5f, 3.4f, 4.6f});
    const auto truncated = std::simd::trunc(std::simd::vec<float, 4>{1.9f, 2.1f, 3.8f, 4.2f});
    const auto sine = std::simd::sin(std::simd::vec<float, 1>{0.0f});
    const auto cosine = std::simd::cos(std::simd::vec<float, 1>{0.0f});
    const auto exponent = std::simd::exp(std::simd::vec<float, 1>{1.0f});
    const auto logarithm = std::simd::log(exponent);
    const auto powered = std::simd::pow(std::simd::vec<float, 4>{2.0f, 3.0f, 4.0f, 5.0f},
                                        std::simd::vec<float, 4>{3.0f, 2.0f, 1.0f, 0.0f});
    const auto clamped = std::simd::clamp(std::simd::vec<float, 4>{-1.0f, 3.0f, 8.0f, 5.0f},
                                          std::simd::vec<float, 4>{0.0f, 1.0f, 2.0f, 3.0f},
                                          std::simd::vec<float, 4>{4.0f, 4.0f, 6.0f, 4.0f});

    EXPECT_FLOAT_EQ(rounded[0], 1.0f);
    EXPECT_FLOAT_EQ(rounded[2], 3.0f);
    EXPECT_FLOAT_EQ(ceiled[0], 2.0f);
    EXPECT_FLOAT_EQ(ceiled[3], 5.0f);
    EXPECT_FLOAT_EQ(rounded_nearest[1], 3.0f);
    EXPECT_FLOAT_EQ(rounded_nearest[3], 5.0f);
    EXPECT_FLOAT_EQ(truncated[0], 1.0f);
    EXPECT_FLOAT_EQ(truncated[3], 4.0f);
    EXPECT_NEAR(sine[0], 0.0f, 1e-6f);
    EXPECT_NEAR(cosine[0], 1.0f, 1e-6f);
    EXPECT_NEAR(exponent[0], std::exp(1.0f), 1e-6f);
    EXPECT_NEAR(logarithm[0], 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(powered[0], 8.0f);
    EXPECT_FLOAT_EQ(powered[3], 1.0f);
    EXPECT_FLOAT_EQ(clamped[0], 0.0f);
    EXPECT_FLOAT_EQ(clamped[2], 6.0f);
}

} // namespace

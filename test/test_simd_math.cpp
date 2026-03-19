#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>

namespace {

using int4 = std::simd::vec<int, 4>;
using mask4 = std::simd::mask<int, 4>;

template<class V, class T, size_t N>
V load_vec(const std::array<T, N>& values) {
    return std::simd::partial_load<V>(values.data(), static_cast<std::simd::simd_size_type>(N));
}

TEST(SimdMathTest, ReduceUsesProvidedBinaryOperation) {
    const std::array<int, 4> data{{2, 3, 4, 5}};
    const int4 values = load_vec<int4>(data);

    EXPECT_EQ(std::simd::reduce(values, std::multiplies<>{}), 120);
}

TEST(SimdMathTest, MaskedReduceStartsFromIdentityElement) {
    const std::array<int, 4> data{{2, 3, 4, 5}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0110u);

    EXPECT_EQ(std::simd::reduce(values, selected), 7);
    EXPECT_EQ(std::simd::reduce(values, selected, std::plus<>{}, 10), 17);
}

TEST(SimdMathTest, ReduceMinAndReduceMaxHonorMaskSelection) {
    const std::array<int, 4> data{{8, 3, 6, 1}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0110u);

    EXPECT_EQ(std::simd::reduce_min(values), 1);
    EXPECT_EQ(std::simd::reduce_max(values), 8);
    EXPECT_EQ(std::simd::reduce_min(values, selected), 3);
    EXPECT_EQ(std::simd::reduce_max(values, selected), 6);
}

TEST(SimdMathTest, BasicMathFunctionsApplyPerLane) {
    using float4 = std::simd::vec<float, 4>;
    const std::array<float, 4> abs_data{{-1.0f, -4.0f, -9.0f, -16.0f}};
    const std::array<float, 4> sqrt_data{{1.0f, 4.0f, 9.0f, 16.0f}};
    const std::array<float, 4> left_data{{1.0f, 5.0f, 3.0f, 7.0f}};
    const std::array<float, 4> right_data{{2.0f, 4.0f, 6.0f, 1.0f}};

    const auto abs_value = std::simd::abs(load_vec<float4>(abs_data));
    const auto sqrt_value = std::simd::sqrt(load_vec<float4>(sqrt_data));
    const auto min_value = std::simd::min(load_vec<float4>(left_data), load_vec<float4>(right_data));
    const auto max_value = std::simd::max(load_vec<float4>(left_data), load_vec<float4>(right_data));

    EXPECT_FLOAT_EQ(abs_value[0], 1.0f);
    EXPECT_FLOAT_EQ(abs_value[3], 16.0f);
    EXPECT_FLOAT_EQ(sqrt_value[1], 2.0f);
    EXPECT_FLOAT_EQ(sqrt_value[3], 4.0f);
    EXPECT_FLOAT_EQ(min_value[1], 4.0f);
    EXPECT_FLOAT_EQ(max_value[2], 6.0f);
}

TEST(SimdMathTest, RoundingAndTranscendentalFunctionsRemainComposable) {
    using float4 = std::simd::vec<float, 4>;
    const std::array<float, 4> floor_data{{1.9f, 2.1f, 3.8f, 4.0f}};
    const std::array<float, 4> ceil_data{{1.2f, 2.0f, 3.1f, 4.8f}};
    const std::array<float, 4> round_data{{1.2f, 2.5f, 3.4f, 4.6f}};
    const std::array<float, 4> trunc_data{{1.9f, 2.1f, 3.8f, 4.2f}};
    const std::array<float, 4> pow_base{{2.0f, 3.0f, 4.0f, 5.0f}};
    const std::array<float, 4> pow_exp{{3.0f, 2.0f, 1.0f, 0.0f}};
    const std::array<float, 4> clamp_value{{-1.0f, 3.0f, 8.0f, 5.0f}};
    const std::array<float, 4> clamp_low{{0.0f, 1.0f, 2.0f, 3.0f}};
    const std::array<float, 4> clamp_high{{4.0f, 4.0f, 6.0f, 4.0f}};

    const auto rounded = std::simd::floor(load_vec<float4>(floor_data));
    const auto ceiled = std::simd::ceil(load_vec<float4>(ceil_data));
    const auto rounded_nearest = std::simd::round(load_vec<float4>(round_data));
    const auto truncated = std::simd::trunc(load_vec<float4>(trunc_data));
    const auto sine = std::simd::sin(std::simd::vec<float, 1>(0.0f));
    const auto cosine = std::simd::cos(std::simd::vec<float, 1>(0.0f));
    const auto exponent = std::simd::exp(std::simd::vec<float, 1>(1.0f));
    const auto logarithm = std::simd::log(exponent);
    const auto powered = std::simd::pow(load_vec<float4>(pow_base), load_vec<float4>(pow_exp));
    const auto clamped = std::simd::clamp(load_vec<float4>(clamp_value),
                                          load_vec<float4>(clamp_low),
                                          load_vec<float4>(clamp_high));

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

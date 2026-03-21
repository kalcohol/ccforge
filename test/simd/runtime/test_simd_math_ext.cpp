#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace {

using namespace simd_test;

TEST(SimdMathExtTest, ExtendedUnaryMathFunctionsApplyPerLane) {
    const float4 log10_values = load_vec<float4>(std::array<float, 4>{{1.0f, 10.0f, 100.0f, 1000.0f}});
    const float4 log1p_values = load_vec<float4>(std::array<float, 4>{{0.0f, 1.0f, 3.0f, 7.0f}});
    const float4 cbrt_values = load_vec<float4>(std::array<float, 4>{{1.0f, 8.0f, 27.0f, 64.0f}});
    const float4 trig_values = load_vec<float4>(std::array<float, 4>{{-0.5f, 0.0f, 0.5f, 0.25f}});
    const float4 acosh_values = load_vec<float4>(std::array<float, 4>{{1.0f, 1.5f, 2.0f, 3.0f}});
    const float4 atanh_values = load_vec<float4>(std::array<float, 4>{{0.0f, 0.25f, -0.5f, 0.75f}});

    const auto log10_result = std::simd::log10(log10_values);
    const auto log1p_result = std::simd::log1p(log1p_values);
    const auto log2_result = std::simd::log2(log10_values);
    const auto logb_result = std::simd::logb(log10_values);
    const auto cbrt_result = std::simd::cbrt(cbrt_values);
    const auto asin_result = std::simd::asin(trig_values);
    const auto acos_result = std::simd::acos(trig_values);
    const auto atan_result = std::simd::atan(trig_values);
    const auto tan_result = std::simd::tan(trig_values);
    const auto sinh_result = std::simd::sinh(trig_values);
    const auto cosh_result = std::simd::cosh(trig_values);
    const auto tanh_result = std::simd::tanh(trig_values);
    const auto asinh_result = std::simd::asinh(trig_values);
    const auto acosh_result = std::simd::acosh(acosh_values);
    const auto atanh_result = std::simd::atanh(atanh_values);
    const auto exp2_result = std::simd::exp2(log10_values);
    const auto expm1_result = std::simd::expm1(trig_values);
    const auto erf_result = std::simd::erf(trig_values);
    const auto erfc_result = std::simd::erfc(trig_values);
    const auto lgamma_result = std::simd::lgamma(acosh_values);
    const auto tgamma_result = std::simd::tgamma(log1p_values);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(log10_result[i], std::log10(log10_values[i]), 1e-6f);
        EXPECT_NEAR(log1p_result[i], std::log1p(log1p_values[i]), 1e-6f);
        EXPECT_NEAR(log2_result[i], std::log2(log10_values[i]), 1e-6f);
        EXPECT_NEAR(logb_result[i], std::logb(log10_values[i]), 1e-6f);
        EXPECT_NEAR(cbrt_result[i], std::cbrt(cbrt_values[i]), 1e-6f);
        EXPECT_NEAR(asin_result[i], std::asin(trig_values[i]), 1e-6f);
        EXPECT_NEAR(acos_result[i], std::acos(trig_values[i]), 1e-6f);
        EXPECT_NEAR(atan_result[i], std::atan(trig_values[i]), 1e-6f);
        EXPECT_NEAR(tan_result[i], std::tan(trig_values[i]), 1e-6f);
        EXPECT_NEAR(sinh_result[i], std::sinh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(cosh_result[i], std::cosh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(tanh_result[i], std::tanh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(asinh_result[i], std::asinh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(acosh_result[i], std::acosh(acosh_values[i]), 1e-6f);
        EXPECT_NEAR(atanh_result[i], std::atanh(atanh_values[i]), 1e-6f);
        EXPECT_NEAR(exp2_result[i], std::exp2(log10_values[i]), 1e-6f);
        EXPECT_NEAR(expm1_result[i], std::expm1(trig_values[i]), 1e-6f);
        EXPECT_NEAR(erf_result[i], std::erf(trig_values[i]), 1e-6f);
        EXPECT_NEAR(erfc_result[i], std::erfc(trig_values[i]), 1e-6f);
        EXPECT_NEAR(lgamma_result[i], std::lgamma(acosh_values[i]), 1e-6f);
        EXPECT_NEAR(tgamma_result[i], std::tgamma(log1p_values[i]), 1e-6f);
    }
}

TEST(SimdMathExtTest, BinaryMathAndClassificationFunctionsApplyPerLane) {
    const float4 left = load_vec<float4>(std::array<float, 4>{{3.0f, 5.5f, -7.0f, 8.0f}});
    const float4 right = load_vec<float4>(std::array<float, 4>{{4.0f, 2.0f, 2.0f, -3.0f}});
    const float4 signs = load_vec<float4>(std::array<float, 4>{{-1.0f, 1.0f, -1.0f, 1.0f}});
    const float4 classification = load_vec<float4>(std::array<float, 4>{
        {0.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1.0f / 32.0f}});

    const auto hypot_result = std::simd::hypot(left, 2.0f);
    const auto hypot3_result = std::simd::hypot(left, 2.0f, 3.0f);
    const auto fmod_result = std::simd::fmod(left, 2.0f);
    const auto remainder_result = std::simd::remainder(left, 2.0f);
    const auto copysign_result = std::simd::copysign(left, signs);
    const auto nextafter_result = std::simd::nextafter(left, 100.0f);
    const auto fdim_result = std::simd::fdim(left, right);
    const auto fmin_result = std::simd::fmin(left, right);
    const auto fmax_result = std::simd::fmax(left, right);
    const auto fma_result = std::simd::fma(left, 2.0f, 1.0f);
    const auto atan2_result = std::simd::atan2(left, right);
    const auto lerp_result = std::simd::lerp(left, right, 0.25f);
    const auto ilogb_result = std::simd::ilogb(classification);
    const auto fpclassify_result = std::simd::fpclassify(classification);
    const auto ldexp_result = std::simd::ldexp(left, load_vec<int4>(std::array<int, 4>{{1, 2, -1, 0}}));
    const auto scalbn_result = std::simd::scalbn(left, load_vec<int4>(std::array<int, 4>{{1, 2, -1, 0}}));
    const auto scalbln_result = std::simd::scalbln(left, load_vec<long4>(std::array<long, 4>{{1, 2, -1, 0}}));
    const auto finite_mask = std::simd::isfinite(classification);
    const auto inf_mask = std::simd::isinf(classification);
    const auto nan_mask = std::simd::isnan(classification);
    const auto normal_mask = std::simd::isnormal(classification);
    const auto sign_mask = std::simd::signbit(load_vec<float4>(std::array<float, 4>{{-1.0f, 1.0f, -0.0f, 0.0f}}));
    const auto greater_mask = std::simd::isgreater(left, right);
    const auto unordered_mask = std::simd::isunordered(classification,
        load_vec<float4>(std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}}));

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(hypot_result[i], std::hypot(left[i], 2.0f), 1e-6f);
        EXPECT_NEAR(hypot3_result[i], std::hypot(left[i], 2.0f, 3.0f), 1e-6f);
        EXPECT_NEAR(fmod_result[i], std::fmod(left[i], 2.0f), 1e-6f);
        EXPECT_NEAR(remainder_result[i], std::remainder(left[i], 2.0f), 1e-6f);
        EXPECT_EQ(copysign_result[i], std::copysign(left[i], signs[i]));
        EXPECT_EQ(nextafter_result[i], std::nextafter(left[i], 100.0f));
        EXPECT_NEAR(fdim_result[i], std::fdim(left[i], right[i]), 1e-6f);
        EXPECT_EQ(fmin_result[i], std::fmin(left[i], right[i]));
        EXPECT_EQ(fmax_result[i], std::fmax(left[i], right[i]));
        EXPECT_NEAR(fma_result[i], std::fma(left[i], 2.0f, 1.0f), 1e-6f);
        EXPECT_NEAR(atan2_result[i], std::atan2(left[i], right[i]), 1e-6f);
        EXPECT_NEAR(lerp_result[i], std::lerp(left[i], right[i], 0.25f), 1e-6f);
        EXPECT_EQ(ilogb_result[i], std::ilogb(classification[i]));
        EXPECT_EQ(fpclassify_result[i], std::fpclassify(classification[i]));
        EXPECT_NEAR(ldexp_result[i], std::ldexp(left[i], std::array<int, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
        EXPECT_NEAR(scalbn_result[i], std::scalbn(left[i], std::array<int, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
        EXPECT_NEAR(scalbln_result[i], std::scalbln(left[i], std::array<long, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
    }

    EXPECT_TRUE(finite_mask[0]);
    EXPECT_FALSE(finite_mask[1]);
    EXPECT_FALSE(finite_mask[2]);
    EXPECT_TRUE(finite_mask[3]);

    EXPECT_FALSE(inf_mask[0]);
    EXPECT_TRUE(inf_mask[1]);
    EXPECT_FALSE(inf_mask[2]);
    EXPECT_FALSE(inf_mask[3]);

    EXPECT_FALSE(nan_mask[0]);
    EXPECT_FALSE(nan_mask[1]);
    EXPECT_TRUE(nan_mask[2]);
    EXPECT_FALSE(nan_mask[3]);

    EXPECT_FALSE(normal_mask[0]);
    EXPECT_FALSE(normal_mask[1]);
    EXPECT_FALSE(normal_mask[2]);
    EXPECT_TRUE(normal_mask[3]);

    EXPECT_TRUE(sign_mask[0]);
    EXPECT_FALSE(sign_mask[1]);
    EXPECT_TRUE(sign_mask[2]);
    EXPECT_FALSE(sign_mask[3]);

    EXPECT_FALSE(greater_mask[0]);
    EXPECT_TRUE(greater_mask[1]);
    EXPECT_FALSE(greater_mask[2]);
    EXPECT_TRUE(greater_mask[3]);

    EXPECT_FALSE(unordered_mask[0]);
    EXPECT_FALSE(unordered_mask[1]);
    EXPECT_TRUE(unordered_mask[2]);
    EXPECT_FALSE(unordered_mask[3]);
}

TEST(SimdMathExtTest, DecompositionFunctionsReturnLaneWiseResults) {
    const float4 frexp_values = load_vec<float4>(std::array<float, 4>{{1.0f, 2.0f, 3.0f, 8.0f}});
    const float4 modf_values = load_vec<float4>(std::array<float, 4>{{1.5f, -2.25f, 3.0f, -4.75f}});
    const float4 remquo_values = load_vec<float4>(std::array<float, 4>{{5.3f, 7.9f, -4.2f, 9.5f}});

    int4 exponents;
    float4 integral;
    int4 quotients;

    const auto fractions = std::simd::frexp(frexp_values, &exponents);
    const auto fractional = std::simd::modf(modf_values, &integral);
    const auto remainders = std::simd::remquo(remquo_values, 2.0f, &quotients);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        int expected_exp = 0;
        int expected_quo = 0;
        float expected_integral = 0.0f;

        const float expected_fraction = std::frexp(frexp_values[i], &expected_exp);
        const float expected_fractional = std::modf(modf_values[i], &expected_integral);
        const float expected_remainder = std::remquo(remquo_values[i], 2.0f, &expected_quo);

        EXPECT_NEAR(fractions[i], expected_fraction, 1e-6f);
        EXPECT_EQ(exponents[i], expected_exp);
        EXPECT_NEAR(fractional[i], expected_fractional, 1e-6f);
        EXPECT_NEAR(integral[i], expected_integral, 1e-6f);
        EXPECT_NEAR(remainders[i], expected_remainder, 1e-6f);
        EXPECT_EQ(quotients[i], expected_quo);
    }
}

} // namespace

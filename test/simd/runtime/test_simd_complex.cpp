#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>

namespace {

using namespace simd_test;

complex4f load_complex4(const std::array<std::complex<float>, 4>& values) {
    return std::simd::partial_load<complex4f>(values.data(), 4);
}

TEST(SimdComplexTest, ConstructsAndAccessorsRoundTripRealAndImaginaryParts) {
    const float4 reals = load_vec<float4>(std::array<float, 4>{{1.0f, 2.0f, 3.0f, 4.0f}});
    const float4 imags = load_vec<float4>(std::array<float, 4>{{-1.0f, -2.0f, -3.0f, -4.0f}});

    complex4f value(reals, imags);
    const auto extracted_reals = value.real();
    const auto extracted_imags = std::simd::imag(value);

    for (std::simd::simd_size_type i = 0; i < complex4f::size; ++i) {
        EXPECT_FLOAT_EQ(value[i].real(), reals[i]);
        EXPECT_FLOAT_EQ(value[i].imag(), imags[i]);
        EXPECT_FLOAT_EQ(extracted_reals[i], reals[i]);
        EXPECT_FLOAT_EQ(extracted_imags[i], imags[i]);
    }

    value.real(load_vec<float4>(std::array<float, 4>{{5.0f, 6.0f, 7.0f, 8.0f}}));
    value.imag(load_vec<float4>(std::array<float, 4>{{0.5f, 1.5f, 2.5f, 3.5f}}));

    EXPECT_FLOAT_EQ(value[0].real(), 5.0f);
    EXPECT_FLOAT_EQ(value[3].real(), 8.0f);
    EXPECT_FLOAT_EQ(value[0].imag(), 0.5f);
    EXPECT_FLOAT_EQ(value[3].imag(), 3.5f);
}

TEST(SimdComplexTest, ComplexAlgorithmsApplyPerLane) {
    const std::array<std::complex<float>, 4> raw{{
        {1.0f, 2.0f},
        {-2.5f, 0.5f},
        {0.25f, -1.5f},
        {3.0f, -4.0f},
    }};
    const complex4f values = load_complex4(raw);

    const auto abs_value = std::simd::abs(values);
    const auto arg_value = std::simd::arg(values);
    const auto norm_value = std::simd::norm(values);
    const auto conj_value = std::simd::conj(values);
    const auto proj_value = std::simd::proj(values);
    const auto log10_value = std::simd::log10(values);
    const auto pow_value = std::simd::pow(values, values);

    for (std::simd::simd_size_type i = 0; i < complex4f::size; ++i) {
        EXPECT_NEAR(abs_value[i], std::abs(raw[static_cast<size_t>(i)]), 1e-5f);
        EXPECT_NEAR(arg_value[i], std::arg(raw[static_cast<size_t>(i)]), 1e-5f);
        EXPECT_NEAR(norm_value[i], std::norm(raw[static_cast<size_t>(i)]), 1e-5f);
        EXPECT_NEAR(conj_value[i].real(), std::conj(raw[static_cast<size_t>(i)]).real(), 1e-5f);
        EXPECT_NEAR(conj_value[i].imag(), std::conj(raw[static_cast<size_t>(i)]).imag(), 1e-5f);
        EXPECT_NEAR(proj_value[i].real(), std::proj(raw[static_cast<size_t>(i)]).real(), 1e-5f);
        EXPECT_NEAR(proj_value[i].imag(), std::proj(raw[static_cast<size_t>(i)]).imag(), 1e-5f);
        EXPECT_NEAR(log10_value[i].real(), std::log10(raw[static_cast<size_t>(i)]).real(), 1e-4f);
        EXPECT_NEAR(log10_value[i].imag(), std::log10(raw[static_cast<size_t>(i)]).imag(), 1e-4f);
        EXPECT_NEAR(pow_value[i].real(), std::pow(raw[static_cast<size_t>(i)], raw[static_cast<size_t>(i)]).real(), 1e-3f);
        EXPECT_NEAR(pow_value[i].imag(), std::pow(raw[static_cast<size_t>(i)], raw[static_cast<size_t>(i)]).imag(), 1e-3f);
    }
}

TEST(SimdComplexTest, PolarBuildsComplexLanesFromMagnitudeAndAngle) {
    const float4 magnitudes = load_vec<float4>(std::array<float, 4>{{1.0f, 2.0f, 3.0f, 4.0f}});
    const float4 angles = load_vec<float4>(std::array<float, 4>{{0.0f, 0.5f, -1.0f, 1.5f}});

    const auto with_default_angle = std::simd::polar(magnitudes);
    const auto with_angle = std::simd::polar(magnitudes, angles);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        const auto expected_default = std::polar(magnitudes[i]);
        const auto expected_angle = std::polar(magnitudes[i], angles[i]);

        EXPECT_NEAR(with_default_angle[i].real(), expected_default.real(), 1e-5f);
        EXPECT_NEAR(with_default_angle[i].imag(), expected_default.imag(), 1e-5f);
        EXPECT_NEAR(with_angle[i].real(), expected_angle.real(), 1e-5f);
        EXPECT_NEAR(with_angle[i].imag(), expected_angle.imag(), 1e-5f);
    }
}

} // namespace

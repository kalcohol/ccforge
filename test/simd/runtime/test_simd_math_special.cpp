#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace {

using namespace simd_test;

TEST(SimdMathSpecialTest, SpecialFunctionsApplyPerLane) {
    const uint4 orders = load_vec<uint4>(std::array<unsigned, 4>{{0u, 1u, 2u, 3u}});
    const uint4 degrees = load_vec<uint4>(std::array<unsigned, 4>{{0u, 1u, 1u, 2u}});
    const float4 positive = load_vec<float4>(std::array<float, 4>{{0.1f, 0.2f, 0.3f, 0.4f}});
    const float4 unitish = load_vec<float4>(std::array<float, 4>{{0.1f, 0.2f, 0.3f, 0.4f}});

    const auto comp1 = std::simd::comp_ellint_1(positive);
    const auto comp2 = std::simd::comp_ellint_2(positive);
    const auto expint_value = std::simd::expint(positive);
    const auto zeta = std::simd::riemann_zeta(load_vec<float4>(std::array<float, 4>{{2.0f, 3.0f, 4.0f, 5.0f}}));
    const auto beta_value = std::simd::beta(positive, 0.5f);
    const auto ellint3 = std::simd::ellint_3(positive, 0.25f, 0.5f);
    const auto hermite_value = std::simd::hermite(orders, positive);
    const auto laguerre_value = std::simd::laguerre(orders, positive);
    const auto legendre_value = std::simd::legendre(orders, unitish);
    const auto sph_bessel_value = std::simd::sph_bessel(orders, positive);
    const auto sph_neumann_value = std::simd::sph_neumann(orders, positive);
    const auto assoc_laguerre_value = std::simd::assoc_laguerre(orders, degrees, positive);
    const auto assoc_legendre_value = std::simd::assoc_legendre(orders, degrees, unitish);
    const auto sph_legendre_value = std::simd::sph_legendre(orders, degrees, unitish);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(comp1[i], std::comp_ellint_1(positive[i]), 1e-5f);
        EXPECT_NEAR(comp2[i], std::comp_ellint_2(positive[i]), 1e-5f);
        EXPECT_NEAR(expint_value[i], std::expint(positive[i]), 1e-5f);
        EXPECT_NEAR(zeta[i], std::riemann_zeta(static_cast<float>(i + 2)), 1e-5f);
        EXPECT_NEAR(beta_value[i], std::beta(positive[i], 0.5f), 1e-5f);
        EXPECT_NEAR(ellint3[i], std::ellint_3(positive[i], 0.25f, 0.5f), 1e-5f);
        EXPECT_NEAR(hermite_value[i], std::hermite(orders[i], positive[i]), 1e-5f);
        EXPECT_NEAR(laguerre_value[i], std::laguerre(orders[i], positive[i]), 1e-5f);
        EXPECT_NEAR(legendre_value[i], std::legendre(orders[i], unitish[i]), 1e-5f);
        EXPECT_NEAR(sph_bessel_value[i], std::sph_bessel(orders[i], positive[i]), 1e-5f);
        EXPECT_NEAR(sph_neumann_value[i], std::sph_neumann(orders[i], positive[i]), 1e-5f);
        EXPECT_NEAR(assoc_laguerre_value[i], std::assoc_laguerre(orders[i], degrees[i], positive[i]), 1e-5f);
        EXPECT_NEAR(assoc_legendre_value[i], std::assoc_legendre(orders[i], degrees[i], unitish[i]), 1e-5f);
        EXPECT_NEAR(sph_legendre_value[i], std::sph_legendre(orders[i], degrees[i], unitish[i]), 1e-5f);
    }
}

TEST(SimdMathSpecialTest, VectorizedSpecialMathCommonResultTypeMatchesScalarSemantics) {
    const float4 left = load_vec<float4>(std::array<float, 4>{{0.1f, 0.2f, 0.3f, 0.4f}});
    const float4 right = load_vec<float4>(std::array<float, 4>{{0.2f, 0.3f, 0.4f, 0.5f}});

    const auto comp3 = std::simd::comp_ellint_3(left, right);
    const auto cyl_i = std::simd::cyl_bessel_i(left, right);
    const auto cyl_j = std::simd::cyl_bessel_j(left, right);
    const auto cyl_k = std::simd::cyl_bessel_k(left, right);
    const auto cyl_n = std::simd::cyl_neumann(left, right);
    const auto ellint1 = std::simd::ellint_1(left, right);
    const auto ellint2 = std::simd::ellint_2(left, right);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(comp3[i], std::comp_ellint_3(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_i[i], std::cyl_bessel_i(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_j[i], std::cyl_bessel_j(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_k[i], std::cyl_bessel_k(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_n[i], std::cyl_neumann(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(ellint1[i], std::ellint_1(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(ellint2[i], std::ellint_2(left[i], right[i]), 1e-5f);
    }
}

} // namespace

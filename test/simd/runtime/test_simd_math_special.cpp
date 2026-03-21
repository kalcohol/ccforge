#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace {

using namespace simd_test;

template<class T>
void expect_near_array(const char* label,
                       const float4& actual,
                       const std::array<T, 4>& expected,
                       float tolerance) {
    SCOPED_TRACE(label);
    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(actual[i], static_cast<float>(expected[static_cast<size_t>(i)]), tolerance);
    }
}

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

#if defined(__cpp_lib_math_special_functions)
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
#else
    expect_near_array("comp_ellint_1", comp1, std::array<double, 4>{{
        1.5747455615173558, 1.5868678474541664, 1.6080486199305128, 1.6399998658645116}}, 2e-4f);
    expect_near_array("comp_ellint_2", comp2, std::array<double, 4>{{
        1.5668619420216681, 1.5549685462425296, 1.5348334649232491, 1.5059416123600406}}, 2e-4f);
    expect_near_array("expint", expint_value, std::array<double, 4>{{
        -1.6228128139692766, -0.8217605879024001, -0.30266853926582593, 0.10476521861932497}}, 2e-4f);
    expect_near_array("riemann_zeta", zeta, std::array<double, 4>{{
        1.6449340668482264, 1.2020569031595942, 1.0823232337111379, 1.03692775514337}}, 2e-4f);
    expect_near_array("beta", beta_value, std::array<double, 4>{{
        11.323086975215746, 6.2686531240860335, 4.5544430879621718, 3.6790939804058809}}, 2e-4f);
    expect_near_array("ellint_3", ellint3, std::array<double, 4>{{
        0.51047544419492774, 0.51109490896838605, 0.51213620382181702, 0.51361300641311136}}, 2e-4f);
    expect_near_array("hermite", hermite_value, std::array<double, 4>{{1.0, 0.4, -1.64, -4.288}}, 2e-4f);
    expect_near_array("laguerre", laguerre_value, std::array<double, 4>{{1.0, 0.8, 0.445, 0.029333333333333378}}, 2e-4f);
    expect_near_array("legendre", legendre_value, std::array<double, 4>{{1.0, 0.2, -0.365, -0.44}}, 2e-4f);
    expect_near_array("sph_bessel", sph_bessel_value, std::array<double, 4>{{
        0.99833416646828155, 0.066400380670322223, 0.0059615248686202185, 0.00060412548152544913}}, 2e-4f);
    expect_near_array("sph_neumann", sph_neumann_value, std::array<double, 4>{{
        -9.9500416527802589, -25.495011100006355, -112.81471738336003, -595.44076702127586}}, 5e-3f);
    expect_near_array("assoc_laguerre", assoc_laguerre_value, std::array<double, 4>{{
        1.0, 1.8, 2.145, 6.3893333333333313}}, 2e-4f);
    expect_near_array("assoc_legendre", assoc_legendre_value, std::array<double, 4>{{
        1.0, 0.9797958971132712, 0.85854528127525109, 5.04}}, 2e-4f);
    expect_near_array("sph_legendre", sph_legendre_value, std::array<double, 4>{{
        0.28209479177387814, -0.068639091469079067, -0.21810682083906741, 0.14274664910800339}}, 2e-4f);
#endif
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

#if defined(__cpp_lib_math_special_functions)
    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(comp3[i], std::comp_ellint_3(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_i[i], std::cyl_bessel_i(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_j[i], std::cyl_bessel_j(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_k[i], std::cyl_bessel_k(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(cyl_n[i], std::cyl_neumann(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(ellint1[i], std::ellint_1(left[i], right[i]), 1e-5f);
        EXPECT_NEAR(ellint2[i], std::ellint_2(left[i], right[i]), 1e-5f);
    }
#else
    expect_near_array("comp_ellint_3", comp3, std::array<double, 4>{{
        1.7608656115083419, 1.8983924169967104, 2.0822121773175528, 2.3367461373176517}}, 3e-4f);
    expect_near_array("cyl_bessel_i", cyl_i, std::array<double, 4>{{
        0.8425563289943494, 0.75928415645914016, 0.70886468373822509, 0.67660336054181136}}, 3e-4f);
    expect_near_array("cyl_bessel_j", cyl_j, std::array<double, 4>{{
        0.82737542099213468, 0.73133734784749449, 0.66655394437693849, 0.61880176080535454}}, 3e-4f);
    expect_near_array("cyl_bessel_k", cyl_k, std::array<double, 4>{{
        1.7722259156803253, 1.4204576140205973, 1.1879742935491502, 1.0186278103166089}}, 3e-4f);
    expect_near_array("cyl_neumann", cyl_n, std::array<double, 4>{{
        -1.22368514019965, -1.0693325145592787, -0.97182982728722078, -0.90269103008817742}}, 3e-4f);
    expect_near_array("ellint_1", ellint1, std::array<double, 4>{{
        0.20001322942737812, 0.3001770675788562, 0.40093555561516647, 0.50322504533421863}}, 3e-4f);
    expect_near_array("ellint_2", ellint2, std::array<double, 4>{{
        0.19998677214287169, 0.29982311912964155, 0.39906832517132146, 0.49681142727296684}}, 3e-4f);
#endif
}

} // namespace

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>

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

TEST(SimdMathSpecialTest, CylindricalBesselFunctionsRemainStableAtLargeArguments) {
    const float4 orders = load_vec<float4>(
        std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}});
    const float4 arguments = load_vec<float4>(
        std::array<float, 4>{{30.0f, 40.0f, 50.0f, 80.0f}});

    const auto j = std::simd::cyl_bessel_j(orders, arguments);
    const auto y = std::simd::cyl_neumann(orders, arguments);
    const auto k = std::simd::cyl_bessel_k(orders, arguments);
    const std::array<double, 4> expected_j{{
        -0.086367983581039975,
        0.0073668905842394303,
        0.055812327669249769,
        -0.069742165512205884}};
    const std::array<double, 4> expected_y{{
        -0.11729573168666423,
        0.12593641705826081,
        -0.098064995470078242,
        -0.05562033908977522}};
    const std::array<double, 4> expected_k{{
        2.1324774964630563e-14,
        8.39286110009957e-19,
        3.4101677497894956e-23,
        2.5251198425054723e-36}};

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        const auto index = static_cast<std::size_t>(i);
        EXPECT_NEAR(j[i], static_cast<float>(expected_j[index]), 5e-6f);
        EXPECT_NEAR(y[i], static_cast<float>(expected_y[index]), 5e-6f);
        EXPECT_NEAR(
            k[i],
            static_cast<float>(expected_k[index]),
            std::max(1e-40f, static_cast<float>(expected_k[index] * 2e-5)));
    }
}

TEST(SimdMathSpecialTest, CylindricalBesselFunctionsCoverHighOrderTransitionBand) {
    const double4 orders = load_vec<double4>(
        std::array<double, 4>{{11.0, 20.0, 40.0, 50.0}});
    const double4 arguments = load_vec<double4>(
        std::array<double, 4>{{27.5, 40.0, 60.0, 80.0}});
    const auto j = std::simd::cyl_bessel_j(orders, arguments);
    const auto y = std::simd::cyl_neumann(orders, arguments);
    constexpr std::array<double, 4> expected_j{{
        0.097996970459618742,
        0.12779393355084890,
        -0.077646197404715064,
        -0.039457764590251248}};
    constexpr std::array<double, 4> expected_y{{
        -0.12507464721647156,
        0.045161820565805892,
        -0.090545084909696294,
        -0.092924250967987226}};

    for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
        const auto index = static_cast<std::size_t>(i);
        EXPECT_NEAR(j[i], expected_j[index], 3e-13);
        EXPECT_NEAR(y[i], expected_y[index], 3e-13);
    }
}

TEST(SimdMathSpecialTest, CylindricalBesselFloatFallbackCoversFormerSignAndPrecisionFailures) {
    const float4 orders = load_vec<float4>(
        std::array<float, 4>{{0.0f, 10.0f, 0.0f, 10.0f}});
    const float4 arguments = load_vec<float4>(
        std::array<float, 4>{{11.9f, 20.0f, 11.9f, 20.0f}});

    const auto j = std::simd::cyl_bessel_j(orders, arguments);
    const auto y = std::simd::cyl_neumann(orders, arguments);
    constexpr std::array<float, 4> expected_j{{
        0.0250494417f,
        0.1864825580f,
        0.0250494417f,
        0.1864825580f}};
    constexpr std::array<float, 4> expected_y{{
        -0.2298332139f,
        -0.0438946535f,
        -0.2298332139f,
        -0.0438946535f}};

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        const auto index = static_cast<std::size_t>(i);
        EXPECT_NEAR(j[i], expected_j[index], 5e-7f);
        EXPECT_NEAR(y[i], expected_y[index], 5e-7f);
    }
    EXPECT_LT(y[1], 0.0f);
}

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
TEST(SimdMathSpecialTest, BesselSeriesPreservesSmallResultsAndFloatRange) {
    namespace sm = std::simd::detail::special_math;

    const double i140 = sm::cyl_bessel_i_series(140.0, 40.0);
    const double i100 = sm::cyl_bessel_i_series(100.0, 50.0);
    const float i35f = sm::cyl_bessel_i_series(35.0f, 30.0f);
    const float i40f = sm::cyl_bessel_i_series(40.0f, 8.0f);

    EXPECT_NEAR(
        i140,
        1.7184528001498766e-58,
        1.7184528001498766e-58 * 2e-12);
    EXPECT_NEAR(
        i100,
        2.7278879470968845e-16,
        2.7278879470968845e-16 * 2e-12);
    EXPECT_NEAR(i35f, 4710.137416f, 4710.137416f * 2e-6f);
    EXPECT_NEAR(i40f, 2.185028153e-24f, 2.185028153e-24f * 2e-6f);
}

TEST(SimdMathSpecialTest, BesselTinyArgumentFallbackAvoidsCancellation) {
    namespace sm = std::simd::detail::special_math;

    const float moderate =
        sm::cyl_bessel_j_fallback(0.3137f, 1.0e-8f);
    const float tiny =
        sm::cyl_bessel_j_fallback(0.49f, 1.0e-30f);

    EXPECT_NEAR(moderate, 0.00277911976965f, 8e-9f);
    EXPECT_GT(tiny, 0.0f);
    EXPECT_NEAR(tiny, 1.6035720e-15f, 1e-20f);
}

#if !defined(__cpp_lib_math_special_functions)
TEST(SimdMathSpecialTest, PublicBesselApiExercisesForgeFallback) {
    const double4 double_orders = load_vec<double4>(
        std::array<double, 4>{{140.0, 100.0, 0.3137, 0.49}});
    const double4 double_arguments = load_vec<double4>(
        std::array<double, 4>{{40.0, 50.0, 14.0, 1.0e-30}});
    const auto double_i =
        std::simd::cyl_bessel_i(double_orders, double_arguments);
    const auto double_j =
        std::simd::cyl_bessel_j(double_orders, double_arguments);

    EXPECT_NEAR(
        double_i[0],
        1.7184528001498766e-58,
        1.7184528001498766e-58 * 2e-12);
    EXPECT_NEAR(
        double_i[1],
        2.7278879470968845e-16,
        2.7278879470968845e-16 * 2e-12);
    EXPECT_NEAR(double_j[2], 0.21080637129457458, 5e-13);
    EXPECT_GT(double_j[3], 0.0);

    const float4 float_orders = load_vec<float4>(
        std::array<float, 4>{{35.0f, 40.0f, 0.3137f, 0.49f}});
    const float4 float_arguments = load_vec<float4>(
        std::array<float, 4>{{30.0f, 8.0f, 1.0e-8f, 1.0e-30f}});
    const auto float_i =
        std::simd::cyl_bessel_i(float_orders, float_arguments);
    const auto float_j =
        std::simd::cyl_bessel_j(float_orders, float_arguments);

    EXPECT_NEAR(float_i[0], 4710.137416f, 4710.137416f * 2e-6f);
    EXPECT_NEAR(float_i[1], 2.185028153e-24f, 2.185028153e-24f * 2e-6f);
    EXPECT_NEAR(float_j[2], 0.00277911976965f, 8e-9f);
    EXPECT_NEAR(float_j[3], 1.6035720e-15f, 1e-20f);
}
#endif

TEST(SimdMathSpecialTest, BesselLargeOrderTerminatesBeforeIntegerConversion) {
    namespace sm = std::simd::detail::special_math;

    EXPECT_TRUE(std::isnan(sm::cyl_bessel_j_fallback(5.0e9, 3.0)));
    EXPECT_TRUE(std::isnan(sm::cyl_bessel_y_fallback(5.0e9, 3.0)));
}

TEST(SimdMathSpecialTest, ExpintFallbackStaysStableBeyondPowerSeriesBoundary) {
    EXPECT_NEAR(
        std::simd::detail::special_math::expint_fallback(10.0),
        2492.2289762418777,
        1e-10);
    EXPECT_NEAR(
        std::simd::detail::special_math::expint_fallback(-10.0),
        -4.156968929685325e-6,
        1e-12);
}

TEST(SimdMathSpecialTest, BesselFallbacksRemainStableOutsideSmallArguments) {
    EXPECT_NEAR(
        std::simd::detail::special_math::sph_bessel_fallback(8u, 0.5),
        1.126143960212129e-10,
        1e-14);
    EXPECT_NEAR(
        std::simd::detail::special_math::cyl_bessel_j_series(0.0, 20.0),
        0.16702466434058344,
        1e-10);
    EXPECT_NEAR(
        std::simd::detail::special_math::cyl_bessel_y_fallback(2.0, 3.0),
        -0.16040039348492402,
        1e-10);
    EXPECT_NEAR(
        std::simd::detail::special_math::cyl_bessel_k_fallback(2.0, 3.0),
        0.061510458471742052,
        1e-10);
}

TEST(SimdMathSpecialTest, BesselFallbacksMatchIndependentLargeArgumentReferences) {
    struct reference {
        double order;
        double argument;
        double j;
        double y;
        double k;
    };
    constexpr reference cases[] = {
        {0.0, 16.0, -0.17489907398362922, 0.095810997080712376,
         3.4994116639364986e-08},
        {0.0, 30.0, -0.086367983581039975, -0.11729573168666423,
         2.1324774964630563e-14},
        {0.0, 50.0, 0.055812327669249769, -0.098064995470078242,
         3.4101677497894956e-23},
        {0.5, 30.0, -0.14392965337039978, -0.022470290598831624,
         2.1412375659560111e-14},
        {2.5, 16.0, 0.092572681583959579, -0.17801902369130165,
         4.2285030375216419e-08},
        {5.0, 25.0, -0.066007995398423697, -0.14705799311372242,
         5.6485921365284157e-12},
        {10.0, 100.0, -0.054732176935467808, 0.058331574236418902,
         7.6554279773881018e-45},
    };

    for (const auto& value : cases) {
        SCOPED_TRACE(
            "order=" + std::to_string(value.order) +
            ", argument=" + std::to_string(value.argument));
        const auto tolerance = [](double expected) {
            return std::max(1e-300, std::abs(expected) * 5e-12);
        };
        EXPECT_NEAR(
            std::simd::detail::special_math::cyl_bessel_j_fallback(
                value.order, value.argument),
            value.j,
            tolerance(value.j));
        EXPECT_NEAR(
            std::simd::detail::special_math::cyl_bessel_y_fallback(
                value.order, value.argument),
            value.y,
            tolerance(value.y));
        EXPECT_NEAR(
            std::simd::detail::special_math::cyl_bessel_k_fallback(
                value.order, value.argument),
            value.k,
            tolerance(value.k));
    }
}

TEST(SimdMathSpecialTest, BesselFallbacksMatchHighOrderReferences) {
    struct reference {
        double order;
        double argument;
        double j;
        double y;
    };
    constexpr reference cases[] = {
        {10.5, 26.5, 0.052056953767819247, -0.15310930260804576},
        {12.0, 30.0, 0.14825335109966010, 0.034143171346460223},
        {15.0, 35.0, 0.031442018146929440, 0.13833502839668673},
        {20.0, 2.0, 3.9189728050907538e-19, -4.0816513889983666e16},
        {20.0, 50.0, -0.11670435275957974, 0.016442633948115776},
        {50.0, 50.0, 0.12140902189761506, -0.21031655464397741},
        {100.0, 99.0, 0.077687161700459401, -0.20107219957383567},
        {100.0, 101.0, 0.11480132142789915, -0.13322738381561640},
    };

    for (const auto& value : cases) {
        SCOPED_TRACE(
            "order=" + std::to_string(value.order) +
            ", argument=" + std::to_string(value.argument));
        const auto tolerance = [](double expected) {
            return std::max(2e-14, std::abs(expected) * 2e-12);
        };
        EXPECT_NEAR(
            std::simd::detail::special_math::cyl_bessel_j_fallback(
                value.order, value.argument),
            value.j,
            tolerance(value.j));
        EXPECT_NEAR(
            std::simd::detail::special_math::cyl_bessel_y_fallback(
                value.order, value.argument),
            value.y,
            tolerance(value.y));
    }
}

TEST(SimdMathSpecialTest, BesselFallbacksSatisfyWronskianAcrossRegimes) {
    constexpr std::pair<double, double> cases[] = {
        {0.3137, 1.0e-8},
        {0.49, 0.01},
        {0.2, 0.1},
        {2.5, 3.0},
        {0.9, 16.1},
        {20.0, 20.0},
        {50.0, 49.5},
        {100.0, 101.0},
    };

    for (const auto [order, argument] : cases) {
        SCOPED_TRACE(
            "order=" + std::to_string(order) +
            ", argument=" + std::to_string(argument));
        const auto current =
            std::simd::detail::special_math::cyl_bessel_jy_fallback(
                order, argument);
        const auto next =
            std::simd::detail::special_math::cyl_bessel_jy_fallback(
                order + 1.0, argument);
        const double expected =
            -2.0 / (std::numbers::pi * argument);
        const double actual =
            current.j * next.y - next.j * current.y;

        EXPECT_NEAR(
            actual,
            expected,
            std::max(2e-14, std::abs(expected) * 2e-12));
    }
}

TEST(SimdMathSpecialTest, BesselFallbacksSatisfyOrderRecurrenceAcrossRegimes) {
    constexpr std::pair<double, double> cases[] = {
        {1.0, 0.01},
        {2.5, 0.5},
        {5.0, 10.0},
        {10.5, 16.1},
        {20.0, 30.0},
        {50.0, 100.0},
    };

    for (const auto [order, argument] : cases) {
        SCOPED_TRACE(
            "order=" + std::to_string(order) +
            ", argument=" + std::to_string(argument));
        const auto previous =
            std::simd::detail::special_math::cyl_bessel_j_fallback(
                order - 1.0, argument);
        const auto current =
            std::simd::detail::special_math::cyl_bessel_j_fallback(
                order, argument);
        const auto next =
            std::simd::detail::special_math::cyl_bessel_j_fallback(
                order + 1.0, argument);
        const double expected =
            2.0 * order / argument * current;
        const double actual = previous + next;
        const double scale =
            std::max({std::abs(actual), std::abs(expected), 1e-300});

        EXPECT_NEAR(actual, expected, scale * 2e-10);
    }
}

TEST(SimdMathSpecialTest, ModifiedBesselKUsesStableHighOrderRoute) {
    struct reference {
        double order;
        double argument;
        double value;
    };
    constexpr reference cases[] = {
        {20.0, 2.0, 5.7708568527002410e16},
        {20.0, 50.0, 1.7061483797220351e-21},
        {40.0, 120.0, 6.3193916548716839e-51},
        {50.0, 125.0, 1.0814480519541901e-51},
    };

    for (const auto& value : cases) {
        SCOPED_TRACE(
            "order=" + std::to_string(value.order) +
            ", argument=" + std::to_string(value.argument));
        const auto actual =
            std::simd::detail::special_math::cyl_bessel_k_fallback(
                value.order, value.argument);
        EXPECT_NEAR(
            actual,
            value.value,
            std::abs(value.value) * 2e-12);
    }
}
#endif

} // namespace

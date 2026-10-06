#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

TEST(SimdMathSpecialTest, SpecialFallbacksHandleDefinedExtremeParameters) {
    namespace sm = std::simd::detail::special_math;

    EXPECT_EQ(sm::assoc_legendre_fallback(3u, 4u, 0.25), 0.0);

    constexpr double large = 1.0e305;
    EXPECT_EQ(sm::beta_fallback(large, 1.0), 1.0 / large);
    EXPECT_EQ(sm::beta_fallback(1.0, large), 1.0 / large);
    constexpr double moderate = 1.0e8;
    const double expected = 1.0 / (moderate * (moderate + 1.0));
    EXPECT_NEAR(sm::beta_fallback(moderate, 2.0),
                expected,
                expected * 3e-14);
    EXPECT_EQ(sm::beta_fallback(2.5e305, 2.5e305), 0.0);
}

TEST(SimdMathSpecialTest, EllipticQuadratureKeepsWorkingPrecisionAtEveryNode) {
    namespace sm = std::simd::detail::special_math;
    unsigned evaluations = 0;
    const auto integrand = [&](long double theta) {
        ++evaluations;
        const long double sine = std::sin(theta);
        return std::sqrt(1.0L - 0.36L * sine * sine);
    };
    const float actual = sm::elliptic_integral(0.7f, integrand);
    // Independent high-precision E(0.7f, 0.6^2), using the exact float angle.
    EXPECT_FLOAT_EQ(actual, static_cast<float>(0.68088891912542322600L));
    EXPECT_LT(evaluations, 3000u);
}

TEST(SimdMathSpecialTest, FloatEllipticQuadraturePreservesIndependentReferences) {
    namespace sm = std::simd::detail::special_math;
    struct sample {
        float modulus;
        float order;
        float amplitude;
        long double second;
        long double third;
    };
    // References use the exact binary float inputs, not decimal approximations.
    const sample samples[]{
        {0.6f, 0.3f, 0.7f, 0.68088891756817366905L, 0.75552973363324815838L},
        {0.9f, 0.5f, 1.2f, 0.99580330253529232071L, 1.94962017222484254355L},
        {0.25f, -0.5f, 4.0f, 3.94460616172860643747L, 3.38837659249246461268L}};
    for (const auto& value : samples) {
        SCOPED_TRACE(value.amplitude);
        EXPECT_FLOAT_EQ(sm::ellint_2_fallback(value.modulus, value.amplitude),
            static_cast<float>(value.second));
        EXPECT_FLOAT_EQ(sm::ellint_3_fallback(value.modulus, value.order, value.amplitude),
            static_cast<float>(value.third));
        EXPECT_FLOAT_EQ(sm::ellint_2_fallback(-value.modulus, -value.amplitude),
            -static_cast<float>(value.second));
        EXPECT_FLOAT_EQ(sm::ellint_3_fallback(-value.modulus, value.order, -value.amplitude),
            -static_cast<float>(value.third));
#if !defined(__cpp_lib_math_special_functions)
        const auto second = std::simd::ellint_2(float4(value.modulus), value.amplitude);
        const auto third = std::simd::ellint_3(float4(value.modulus), value.order, value.amplitude);
        for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
            EXPECT_FLOAT_EQ(second[i], static_cast<float>(value.second));
            EXPECT_FLOAT_EQ(third[i], static_cast<float>(value.third));
        }
#endif
    }
}

TEST(SimdMathSpecialTest, CarlsonPositiveRealHelpersMatchIndependentConstants) {
    namespace sm = std::simd::detail::special_math;
    struct rc_sample { long double x; long double y; long double expected; };
    // Independent mpmath values at 100 decimal digits; inputs are exact binary values.
    const std::array<rc_sample, 5> rc_samples{{
        {0.0L, 4.0L, 0.78539816339744830961566084581987572104929234984377646L},
        {1.0L, 4.0L, 0.60459978807807261686469275254738524409468874936424686L},
        {4.0L, 1.0L, 0.76034599630094634753109425488040582420162773094717643L},
        {2.0L, 3.0L, 0.61547970867038734106746458912399368785517000467754742L},
        {1.0L, 0x1p-80L, 28.419034402957757686106528526827228954921152240722267L}
    }};
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    for (const auto& value : rc_samples) {
        SCOPED_TRACE(value.x);
        SCOPED_TRACE(value.y);
        const long double actual = sm::carlson_rc(value.x, value.y);
        EXPECT_LE(std::abs(actual - value.expected), 128.0L * epsilon * value.expected);
    }
    struct rj_sample {
        long double x; long double y; long double z; long double p;
        long double expected;
    };
    const std::array<rj_sample, 6> rj_samples{{
        {0.0L, 1.0L, 1.0L, 1.0L,
            2.3561944901923449288469825374596271631478770495313294L},
        {1.0L, 2.0L, 3.0L, 4.0L,
            0.23984809974956776217586167104163918463893640226324781L},
        {0.0L, 0.25L, 1.0L, 0.5L,
            6.4695469424989297063160249964009660865923306494784761L},
        {1.0L, 1.0L, 1.0L, 0.25L,
            2.0827679704075707802487540390432465936130218475774114L},
        {1.0L, 2.0L, 3.0L, 0x1p-80L,
            33.908933843746459881446281280224392279858152320214935L},
        {0.0L, 0x1p-40L, 1.0L, 0x1p-80L,
            5433011295025429428.6581394448504783511787236883108402L}
    }};
    for (const auto& value : rj_samples) {
        SCOPED_TRACE(value.x);
        SCOPED_TRACE(value.y);
        SCOPED_TRACE(value.z);
        SCOPED_TRACE(value.p);
        const long double actual = sm::carlson_rj(value.x, value.y, value.z, value.p);
        EXPECT_LE(std::abs(actual - value.expected), 256.0L * epsilon * value.expected);
    }
}

TEST(SimdMathSpecialTest, CarlsonPositiveRealHelpersPreserveDegeneracyAndSymmetry) {
    namespace sm = std::simd::detail::special_math;
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    for (const long double value : {0.25L, 1.0L, 4.0L}) {
        EXPECT_EQ(sm::carlson_rc(value, value), 1.0L / std::sqrt(value));
        EXPECT_EQ(sm::carlson_rj(value, value, value, value),
            1.0L / value / std::sqrt(value));
    }
    for (const auto& value : std::array<std::array<long double, 2>, 3>{{
        {{0.0L, 0.25L}}, {{0.25L, 4.0L}}, {{4.0L, 0.25L}}
    }}) {
        const long double expected = sm::carlson_rf(value[0], value[1], value[1]);
        EXPECT_LE(std::abs(sm::carlson_rc(value[0], value[1]) - expected),
            128.0L * epsilon * expected);
    }
    for (const long double p : {0.25L, 4.0L}) {
        const long double expected = 3.0L * (1.0L - sm::carlson_rc(1.0L, p)) /
            (p - 1.0L);
        EXPECT_LE(std::abs(sm::carlson_rj(1.0L, 1.0L, 1.0L, p) - expected),
            128.0L * epsilon * expected);
    }
    std::array<long double, 3> arguments{{0.0L, 0.25L, 1.0L}};
    const long double expected = sm::carlson_rj(
        arguments[0], arguments[1], arguments[2], 0.5L);
    do {
        EXPECT_LE(std::abs(sm::carlson_rj(
            arguments[0], arguments[1], arguments[2], 0.5L) - expected),
            128.0L * epsilon * expected);
    } while (std::next_permutation(arguments.begin(), arguments.end()));
}

TEST(SimdMathSpecialTest, CarlsonPositiveRealHelpersPreserveHomogeneity) {
    namespace sm = std::simd::detail::special_math;
    constexpr long double rc_reference =
        0.61547970867038734106746458912399368785517000467754742L;
    constexpr long double rj_reference =
        0.23984809974956776217586167104163918463893640226324781L;
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    for (const int exponent : {-600, -120, 0, 120, 600}) {
        SCOPED_TRACE(exponent);
        const long double scale = std::ldexp(1.0L, exponent);
        const long double rc_expected = std::ldexp(rc_reference, -exponent / 2);
        const long double rj_expected = std::ldexp(rj_reference, -3 * exponent / 2);
        EXPECT_LE(std::abs(sm::carlson_rc(2.0L * scale, 3.0L * scale) - rc_expected),
            128.0L * epsilon * rc_expected);
        EXPECT_LE(std::abs(sm::carlson_rc_sqrt(
            std::sqrt(2.0L) * std::sqrt(scale),
            std::sqrt(3.0L) * std::sqrt(scale)) - rc_expected),
            128.0L * epsilon * rc_expected);
        EXPECT_LE(std::abs(sm::carlson_rj(
            scale, 2.0L * scale, 3.0L * scale, 4.0L * scale) - rj_expected),
            256.0L * epsilon * rj_expected);
    }
}

TEST(SimdMathSpecialTest, CarlsonSquareRootRcKeepsTinyPositiveParameters) {
    namespace sm = std::simd::detail::special_math;
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    const long double root = std::numeric_limits<long double>::min();
    // RC(1,root^2) = log(2/root) to well beyond the working precision here.
    const long double rc_expected = std::log(2.0L) - std::log(root);
    const long double rc_actual = sm::carlson_rc_sqrt(1.0L, root);
    ASSERT_TRUE(std::isfinite(rc_actual));
    EXPECT_LE(std::abs(rc_actual - rc_expected), 512.0L * epsilon * rc_expected);

    const long double p = std::numeric_limits<long double>::denorm_min();
    ASSERT_GT(p, 0.0L);
    constexpr long double pi = 3.1415926535897932384626433832795028841971693993751058L;
    // At x=0, RJ(0,y,1,p) ~ 3*pi/(2*sqrt(y*p)); beta^2 underflows at this p.
    const long double rj_expected = 3.0L * pi / std::sqrt(p);
    const long double rj_actual = sm::carlson_rj(0.0L, 0.25L, 1.0L, p);
    ASSERT_TRUE(std::isfinite(rj_actual));
    EXPECT_LE(std::abs(rj_actual - rj_expected), 256.0L * epsilon * rj_expected);
}

TEST(SimdMathSpecialTest, CarlsonPositiveRealHelpersRejectOutsideDomainAndBoundWork) {
    namespace sm = std::simd::detail::special_math;
    const long double nan = std::numeric_limits<long double>::quiet_NaN();
    const long double inf = std::numeric_limits<long double>::infinity();
    for (const auto& value : std::array<std::array<long double, 2>, 7>{{
        {{-1.0L, 1.0L}}, {{1.0L, 0.0L}}, {{1.0L, -1.0L}},
        {{nan, 1.0L}}, {{1.0L, nan}}, {{inf, 1.0L}}, {{1.0L, inf}}
    }}) {
        EXPECT_TRUE(std::isnan(sm::carlson_rc(value[0], value[1])));
        EXPECT_TRUE(std::isnan(sm::carlson_rc_sqrt(value[0], value[1])));
    }
    for (const auto& value : std::array<std::array<long double, 4>, 12>{{
        {{-1.0L, 1.0L, 1.0L, 1.0L}}, {{1.0L, -1.0L, 1.0L, 1.0L}},
        {{1.0L, 1.0L, -1.0L, 1.0L}}, {{1.0L, 1.0L, 1.0L, 0.0L}},
        {{1.0L, 1.0L, 1.0L, -1.0L}}, {{0.0L, 0.0L, 1.0L, 1.0L}},
        {{nan, 1.0L, 1.0L, 1.0L}}, {{1.0L, nan, 1.0L, 1.0L}},
        {{1.0L, 1.0L, nan, 1.0L}}, {{1.0L, 1.0L, 1.0L, nan}},
        {{inf, 1.0L, 1.0L, 1.0L}}, {{1.0L, 1.0L, 1.0L, inf}}
    }}) {
        EXPECT_TRUE(std::isnan(sm::carlson_rj(value[0], value[1], value[2], value[3])));
    }
    // This p-to-argument ratio needs more than 128 duplications, not a partial sum.
    constexpr long double tiny = 0x1p-1022L;
    EXPECT_TRUE(std::isnan(sm::carlson_rj(tiny, tiny, tiny, 1.0L)));
}

TEST(SimdMathSpecialTest, CompleteThirdKindEllipticResolvesJointSingularities) {
    namespace sm = std::simd::detail::special_math;
    struct sample { double modulus; double order; long double expected; };
    // Independent 100-digit values at the exact binary inputs.
    const std::array<sample, 5> samples{{
        {0x1.fffffffffffffp-1, 0x1.fffffffffdcd1p-1,
            4900049181638.88892279462095142171673199846005682359878L},
        {0x1.fffffffffffffp-1, 0x1.fffffff768fa1p-1,
            8353342056.42655063927884378034936980293284158605134L},
        {0x1.fffffffffdcd1p-1, 0x1.fffffff768fa1p-1,
            3803767612.77743341503349641093509640285197214012508L},
        {0x1.fffffffffffffp-1, 0x1.fffffffffffffp-1,
            7074237752028449.97982647125425937802150707026447543L},
        {0x1.ccccccccccccdp-1, 0.75,
            5.23868957169793101481806910344697601119008981687822345L}
    }};
    const auto check = [&]<class T>() {
        for (const auto& value : samples) {
            const T expected = static_cast<T>(value.expected);
            const T actual = sm::comp_ellint_3_fallback(
                static_cast<T>(value.modulus), static_cast<T>(value.order));
            ASSERT_TRUE(std::isfinite(actual));
            EXPECT_LE(std::abs(actual - expected),
                T{256} * std::numeric_limits<T>::epsilon() * expected);
            EXPECT_EQ(sm::comp_ellint_3_fallback(-static_cast<T>(value.modulus),
                static_cast<T>(value.order)), actual);
        }
    };
    check.template operator()<double>();
    check.template operator()<long double>();
    const auto own_neighbors = []<class T>() {
        constexpr int digits = std::numeric_limits<T>::digits;
        if constexpr (digits == 24 || digits == 53 || digits == 64 || digits == 113) {
            constexpr long double reference = [] {
                if constexpr (digits == 24) {
                    return 13176799.3120658849057330924008326896292577223644725274L;
                } else if constexpr (digits == 53) {
                    return 7074237752028449.97982647125425937802150707026447543L;
                } else if constexpr (digits == 64) {
                    return 14488038916154245696.3788673883493037079686119445486943L;
                } else {
                    return 8156040833015188200833743081374156.32640129872067498817L;
                }
            }();
            const T neighbor = std::nextafter(T{1}, T{});
            const T actual = sm::comp_ellint_3_fallback(neighbor, neighbor);
            const T expected = static_cast<T>(reference);
            ASSERT_TRUE(std::isfinite(actual));
            EXPECT_LE(std::abs(actual - expected),
                T{256} * std::numeric_limits<T>::epsilon() * expected);
        }
    };
    own_neighbors.template operator()<float>();
    own_neighbors.template operator()<double>();
    own_neighbors.template operator()<long double>();
    const double modulus = std::nextafter(1.0, 0.0);
    EXPECT_EQ(sm::comp_ellint_3_fallback(modulus, 0.0),
        sm::comp_ellint_1_fallback(modulus));
    EXPECT_EQ(sm::comp_ellint_3_fallback(0.5, -0.5),
        sm::ellint_3_fallback(0.5, -0.5, sm::pi_v<double> / 2));
    EXPECT_TRUE(std::isinf(sm::comp_ellint_3_fallback(0.5, 1.0)));
    EXPECT_TRUE(std::isnan(sm::comp_ellint_3_fallback(0.5,
        std::nextafter(1.0, std::numeric_limits<double>::infinity()))));
    EXPECT_TRUE(std::isinf(sm::comp_ellint_3_fallback(1.0, 0.5)));
#if !defined(__cpp_lib_math_special_functions)
    const std::simd::vec<double, 4> moduli(samples[0].modulus);
    const auto result = std::simd::comp_ellint_3(moduli, samples[0].order);
    for (int lane = 0; lane < 4; ++lane) {
        EXPECT_NEAR(result[lane], static_cast<double>(samples[0].expected),
            static_cast<double>(samples[0].expected) * 5e-14);
    }
#endif
}

TEST(SimdMathSpecialTest, IncompleteFirstKindEllipticResolvesNearSingularEndpoints) {
    namespace sm = std::simd::detail::special_math;
    struct sample {
        double modulus;
        double amplitude;
        long double expected;
    };
    // Independent 90-decimal-digit values at the exact binary input values.
    const std::array<sample, 9> samples{{
        {0.5, 0.3, 0.30111597966406601602586722807201428460L},
        {0.99, 1.55, 3.20969763197262775059865317253388514919L},
        {0.9999999999999999, 1.5707963267948963,
            19.4081210366680757448456591724046652132L},
        {0.9999999999999999, 1.5707963267948968,
            19.4081210664703981325409711450598561439L},
        {0.9999999999999999, 3.141592653589793,
            38.8162421113569393041234537283184538679L},
        {1.0 - 1e-12, 1.570796326795,
            14.8552424629175943009463361997492532769L},
        {1.0 - 1e-10, 2.0, 23.5818399467088761007084300485245757961L},
        {0.999999, 1.56, 5.21742719973809218481407616103783022711L},
        {0.5, 1000.0, 1073.14546387479448636901573956383799650L}
    }};
    const auto check = [&]<class T>() {
        for (const auto& value : samples) {
            SCOPED_TRACE(value.modulus);
            SCOPED_TRACE(value.amplitude);
            const T expected = static_cast<T>(value.expected);
            const T actual = sm::ellint_1_fallback(
                static_cast<T>(value.modulus), static_cast<T>(value.amplitude));
            const T tolerance = std::max(
                T{128} * std::numeric_limits<T>::epsilon(), T{2e-12L}) * expected;
            EXPECT_LE(std::abs(actual - expected), tolerance);
            EXPECT_EQ(sm::ellint_1_fallback(
                static_cast<T>(-value.modulus), static_cast<T>(value.amplitude)), actual);
            EXPECT_EQ(sm::ellint_1_fallback(
                static_cast<T>(value.modulus), static_cast<T>(-value.amplitude)), -actual);
        }
    };
    check.template operator()<double>();
    check.template operator()<long double>();

#if !defined(__cpp_lib_math_special_functions)
    const auto k = load_vec<double4>(std::array<double, 4>{{
        samples[2].modulus, samples[3].modulus, samples[4].modulus, samples[6].modulus}});
    const auto phi = load_vec<double4>(std::array<double, 4>{{
        samples[2].amplitude, samples[3].amplitude, samples[4].amplitude, samples[6].amplitude}});
    const auto actual = std::simd::ellint_1(k, phi);
    const std::array<std::size_t, 4> indices{{2, 3, 4, 6}};
    for (std::simd::simd_size_type lane = 0; lane < double4::size; ++lane) {
        const double expected = static_cast<double>(samples[indices[lane]].expected);
        EXPECT_NEAR(actual[lane], expected, expected * 2e-12);
    }
#endif
}

TEST(SimdMathSpecialTest, FirstKindEllipticFloatAndPeriodIdentitiesStayStable) {
    namespace sm = std::simd::detail::special_math;
    struct sample { float modulus; float amplitude; double expected; };
    const std::array<sample, 3> samples{{
        {0.9999999403953552f, 1.5700000524520874f, 7.78471665892119619999},
        {0.9900000095367432f, 1.5499999523162842f, 3.20969769381920588635},
        {0.9999999403953552f, 2.0f, 17.1915220705822363471}
    }};
    for (const auto& value : samples) {
        EXPECT_NEAR(sm::ellint_1_fallback(value.modulus, value.amplitude),
            static_cast<float>(value.expected), static_cast<float>(value.expected) * 5e-7f);
    }
    for (const double modulus : {0.0, 0.25, 0.9, 1.0 - 1e-14}) {
        const double complete = sm::comp_ellint_1_fallback(modulus);
        for (const double angle : {0.125, 0.75, 1.5}) {
            const double first = sm::ellint_1_fallback(modulus, angle);
            EXPECT_NEAR(sm::ellint_1_fallback(modulus, std::numbers::pi - angle) + first,
                2.0 * complete, complete * 3e-12);
            EXPECT_NEAR(sm::ellint_1_fallback(modulus, std::numbers::pi + angle) - first,
                2.0 * complete, complete * 3e-12);
        }
    }
    EXPECT_TRUE(std::signbit(sm::ellint_1_fallback(0.5, -0.0)));
    EXPECT_EQ(sm::ellint_1_fallback(0.0, 1e300), 1e300);
}

TEST(SimdMathSpecialTest, FirstKindEllipticUsesEachTypesOwnEndpointNeighbors) {
    namespace sm = std::simd::detail::special_math;
    const auto check = []<class T>() {
        constexpr int digits = std::numeric_limits<T>::digits;
        // Keep the oracle inputs independent of a library's numbers constants.
        constexpr T rounded_pi = static_cast<T>(
            3.141592653589793238462643383279502884L);
        if constexpr (digits == 24 || digits == 53 || digits == 64 || digits == 113) {
            constexpr std::array<long double, 3> reference = [] {
                if constexpr (digits == 24) {
                    return std::array<long double, 3>{{
                        9.35726853625712452419961169912096170L,
                        9.35795907021416315949714006438448718L,
                        93.5748726908620045883935610423762318L}};
                } else if constexpr (digits == 53) {
                    return std::array<long double, 3>{{
                        19.4081210366680757448456591724046652L,
                        19.4081210664703981325409711450598561L,
                        194.081210556784696520617268641592269L}};
                } else if constexpr (digits == 64) {
                    return std::array<long double, 3>{{
                        23.2204305485050720519909168624919871L,
                        23.2204305491636165599736361091584915L,
                        232.204305487581678660965133174817873L}};
                } else {
                    return std::array<long double, 3>{{
                        40.2025364724768279291973263671075339L,
                        40.2025364724768279569529019827364474L,
                        402.025364724768279461994630445742428L}};
                }
            }();
            const T modulus = std::nextafter(T{1}, T{});
            const T half_pi = rounded_pi / T{2};
            const std::array<T, 3> angles{{
                std::nextafter(half_pi, T{}),
                std::nextafter(half_pi, std::numeric_limits<T>::infinity()),
                T{5} * rounded_pi}};
            for (std::size_t i = 0; i < angles.size(); ++i) {
                const T expected = static_cast<T>(reference[i]);
                EXPECT_LE(std::abs(sm::ellint_1_fallback(modulus, angles[i]) - expected),
                    T{128} * std::numeric_limits<T>::epsilon() * expected);
            }
        }
        const T modulus = std::nextafter(T{1}, T{});
        const T complete = sm::comp_ellint_1_fallback(modulus);
        for (const T multiplier : {T{1}, T{2}, T{3}, T{5}, T{8}}) {
            const T angle = multiplier * rounded_pi;
            const T before = sm::ellint_1_fallback(modulus, std::nextafter(angle, T{}));
            const T actual = sm::ellint_1_fallback(modulus, angle);
            const T after = sm::ellint_1_fallback(
                modulus, std::nextafter(angle, std::numeric_limits<T>::infinity()));
            EXPECT_LE(before, actual);
            EXPECT_LE(actual, after);
            EXPECT_LE(std::abs(actual - T{2} * multiplier * complete),
                T{64} * std::sqrt(std::numeric_limits<T>::epsilon()) * complete);
        }
    };
    check.template operator()<float>();
    check.template operator()<double>();
    check.template operator()<long double>();
}

#if !defined(__cpp_lib_math_special_functions)
TEST(SimdMathSpecialTest, EllipticFallbackBoundsLargeAmplitudeWork) {
    std::size_t evaluations = 0;
    const auto integral = std::simd::detail::special_math::elliptic_integral(
        1000.0,
        [&](double theta) {
            ++evaluations;
            const double sine = std::sin(theta);
            return 1.0 / std::sqrt(1.0 - 0.25 * sine * sine);
        });

    EXPECT_NEAR(integral, 1073.1454638747948, 2e-9);
    EXPECT_LT(evaluations, 100000u);

    const double4 modulus(0.5);
    const double4 amplitudes = load_vec<double4>(
        std::array<double, 4>{{1000.0, 150.0, -1000.0, 0.0}});
    const auto values = std::simd::ellint_1(modulus, amplitudes);
    const std::array<double, 4> expected{{
        1073.1454638747948,
        161.01584652277398,
        -1073.1454638747948,
        0.0}};
    for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
        EXPECT_NEAR(values[i], expected[static_cast<std::size_t>(i)], 2e-9);
    }
}
#endif

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

TEST(SimdMathSpecialTest, BesselFallbacksPreserveZeroLimitsAndWideOrders) {
    namespace sm = std::simd::detail::special_math;

    const double y0 = sm::cyl_bessel_y_fallback(0.0, 0.0);
    const double y2 = sm::cyl_bessel_y_fallback(2.0, 0.0);
    EXPECT_TRUE(std::isinf(y0));
    EXPECT_TRUE(std::signbit(y0));
    EXPECT_TRUE(std::isinf(y2));
    EXPECT_TRUE(std::signbit(y2));
    EXPECT_EQ(sm::cyl_bessel_j_fallback(5.0e-13, 0.0), 0.0);
    // The I series applies the same exact-zero order discrimination at
    // x == 0 as the J fallback: only nu == 0 gives the limit 1.
    EXPECT_EQ(sm::cyl_bessel_i_series(0.0, 0.0), 1.0);
    EXPECT_EQ(sm::cyl_bessel_i_series(5.0e-13, 0.0), 0.0);

    EXPECT_EQ(sm::cyl_bessel_k_fallback(0.0, 0.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::cyl_bessel_k_fallback(2.0, 0.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::cyl_bessel_i_series(
                  0.0, std::numeric_limits<double>::infinity()),
              std::numeric_limits<double>::infinity());

    const double i1755 = sm::cyl_bessel_i_series(1755.0, 1000.0);
    const double i1800 = sm::cyl_bessel_i_series(1800.0, 2000.0);
    EXPECT_TRUE(std::isfinite(i1755));
    EXPECT_GT(i1755, 0.0);
    EXPECT_EQ(i1800, std::numeric_limits<double>::infinity());

    // Large-argument nonconvergence used to leak NaN between two +inf
    // regions: the unconverged partial sum is a lower bound that already
    // overflows double, so +inf is exact across the former NaN band.
    EXPECT_EQ(sm::cyl_bessel_i_series(0.0, 7000.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::cyl_bessel_i_series(0.0, 7500.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::cyl_bessel_i_series(0.0, 8000.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::cyl_bessel_i_series(0.0, 20000.0),
              std::numeric_limits<double>::infinity());
}

TEST(SimdMathSpecialTest, BesselFallbacksSupportNegativeOrdersAndDomains) {
    namespace sm = std::simd::detail::special_math;

    constexpr double argument = 1.0;
    const double half_order_scale =
        std::sqrt(2.0 / (std::numbers::pi * argument));
    EXPECT_NEAR(sm::cyl_bessel_j_fallback(-0.5, argument),
                half_order_scale * std::cos(argument),
                2e-12);
    EXPECT_NEAR(sm::cyl_bessel_y_fallback(-0.5, argument),
                half_order_scale * std::sin(argument),
                2e-12);
    EXPECT_NEAR(sm::cyl_bessel_j_fallback(-1.0, argument),
                -sm::cyl_bessel_j_fallback(1.0, argument),
                2e-12);
    EXPECT_NEAR(sm::cyl_bessel_y_fallback(-1.0, argument),
                -sm::cyl_bessel_y_fallback(1.0, argument),
                2e-12);

    const double4 orders = load_vec<double4>(
        std::array<double, 4>{{-1.0, -0.5, -1.0, -0.5}});
    const double4 arguments = load_vec<double4>(
        std::array<double, 4>{{1.0, 1.0, 0.0, 0.0}});
    const double4 unit_arguments = load_vec<double4>(
        std::array<double, 4>{{1.0, 1.0, 1.0, 1.0}});
    const auto modified = std::simd::cyl_bessel_i(orders, arguments);
    const auto first_kind =
        std::simd::cyl_bessel_j(orders, unit_arguments);
    const auto second_kind =
        std::simd::cyl_neumann(orders, unit_arguments);
    const auto irregular =
        std::simd::cyl_bessel_k(orders, unit_arguments);
    EXPECT_NEAR(modified[0], sm::cyl_bessel_i_series(1.0, 1.0), 2e-12);
    EXPECT_NEAR(modified[1],
                half_order_scale * std::cosh(argument),
                2e-12);
    EXPECT_EQ(modified[2], 0.0);
    EXPECT_TRUE(std::isinf(modified[3]));
    EXPECT_FALSE(std::signbit(modified[3]));
    EXPECT_NEAR(first_kind[0],
                sm::cyl_bessel_j_fallback(-1.0, 1.0),
                2e-12);
    EXPECT_NEAR(first_kind[1],
                sm::cyl_bessel_j_fallback(-0.5, 1.0),
                2e-12);
    EXPECT_NEAR(second_kind[0],
                sm::cyl_bessel_y_fallback(-1.0, 1.0),
                2e-12);
    EXPECT_NEAR(second_kind[1],
                sm::cyl_bessel_y_fallback(-0.5, 1.0),
                2e-12);
    EXPECT_NEAR(irregular[0],
                sm::cyl_bessel_k_fallback(1.0, 1.0),
                2e-12);
    EXPECT_NEAR(irregular[1],
                sm::cyl_bessel_k_fallback(0.5, 1.0),
                2e-12);
    const double negative_zero_limit =
        sm::cyl_bessel_i_fallback(-1.5, 0.0);
    EXPECT_TRUE(std::isinf(negative_zero_limit));
    EXPECT_TRUE(std::signbit(negative_zero_limit));

    const float float_scale = std::sqrt(
        2.0f / (std::numbers::pi_v<float> * 1.0f));
    EXPECT_NEAR(sm::cyl_bessel_j_fallback(-0.5f, 1.0f),
                float_scale * std::cos(1.0f),
                2e-6f);
    EXPECT_NEAR(sm::cyl_bessel_i_fallback(-1.0f, 1.0f),
                sm::cyl_bessel_i_series(1.0f, 1.0f),
                2e-6f);

    const auto positive_wide = sm::cyl_bessel_jy_fallback(20.0f, 0.01f);
    const auto negative_wide = sm::cyl_bessel_jy_fallback(-20.0f, 0.01f);
    EXPECT_FALSE(std::isnan(negative_wide.j));
    EXPECT_FALSE(std::isnan(negative_wide.y));
    EXPECT_EQ(negative_wide.j, positive_wide.j);
    EXPECT_EQ(negative_wide.y, positive_wide.y);

    EXPECT_TRUE(std::isnan(sm::sph_bessel_fallback(1u, -1.0)));
    EXPECT_TRUE(std::isnan(sm::sph_neumann_fallback(1u, -1.0)));
}

TEST(SimdMathSpecialTest, CompleteEllipticIntegralRemainsStableNearSingularity) {
    namespace sm = std::simd::detail::special_math;

    struct reference {
        double modulus;
        double value;
    };
    const reference cases[] = {
        {1.0 - 1.0e-12, 14.855242389793774},
        {1.0 - 1.0e-15, 18.309508767010367},
        {std::nextafter(1.0, 0.0), 19.408121055678469},
    };

    for (const auto& value : cases) {
        SCOPED_TRACE("modulus=" + std::to_string(value.modulus));
        EXPECT_NEAR(
            sm::comp_ellint_1_fallback(value.modulus),
            value.value,
            value.value * 2e-14);
        EXPECT_NEAR(
            sm::ellint_1_fallback(
                value.modulus,
                std::numbers::pi_v<double> / 2.0),
            value.value,
            value.value * 2e-14);
    }

    EXPECT_EQ(sm::comp_ellint_1_fallback(1.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::ellint_1_fallback(
                  -1.0, -std::numbers::pi_v<double> / 2.0),
              -std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(sm::comp_ellint_1_fallback(1.01)));
}

TEST(SimdMathSpecialTest, IncompleteEllipticGuardsMatchAcrossTheFamily) {
    namespace sm = std::simd::detail::special_math;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // NaN arguments and out-of-domain moduli follow ellint_1's policy in
    // ellint_2/ellint_3 instead of integrating garbage to finite values.
    EXPECT_TRUE(std::isnan(sm::ellint_2_fallback(0.5, nan)));
    EXPECT_TRUE(std::isnan(sm::ellint_3_fallback(0.5, 0.3, nan)));
    EXPECT_TRUE(std::isnan(sm::ellint_2_fallback(nan, 0.1)));
    EXPECT_TRUE(std::isnan(sm::ellint_3_fallback(nan, 0.3, 0.1)));
    EXPECT_TRUE(std::isnan(sm::ellint_2_fallback(2.0, 0.1)));
    EXPECT_TRUE(std::isnan(sm::ellint_3_fallback(2.0, 0.3, 0.1)));

    // F diverges at the |k| == 1 singularity once phi reaches pi/2; a
    // quadrature across the pole used to report finite garbage for
    // phi = 2.0 while phi = pi/2 hit the exact-infinity special case.
    EXPECT_EQ(sm::ellint_1_fallback(1.0, 2.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::ellint_1_fallback(-1.0, -2.0),
              -std::numeric_limits<double>::infinity());
    // Below the pole the incomplete integral stays finite:
    // F(1, phi) = asinh(tan(phi)) for |phi| < pi/2.
    EXPECT_NEAR(sm::ellint_1_fallback(1.0, 1.0),
                std::asinh(std::tan(1.0)), 1e-9);

    const double below_half_pi = std::nextafter(
        std::numbers::pi_v<double> / 2.0, 0.0);
    const double singular_edge = std::asinh(std::tan(below_half_pi));
    EXPECT_NEAR(
        sm::ellint_1_fallback(1.0, below_half_pi),
        singular_edge,
        singular_edge * 2e-15);

    constexpr double edge_nu = 0.5;
    const double edge_sine = std::sin(below_half_pi);
    const double edge_root = std::sqrt(edge_nu);
    const double edge_second = std::atanh(edge_root * edge_sine) / edge_root;
    const double edge_third =
        (singular_edge - edge_nu * edge_second) / (1.0 - edge_nu);
    EXPECT_NEAR(
        sm::ellint_3_fallback(1.0, edge_nu, below_half_pi),
        edge_third,
        edge_third * 2e-15);

    // Pi shares the |k| == 1 divergence: for nu < 1 the pole factor stays
    // nonnegative on the way to pi/2, so the integral is +/-infinity there
    // instead of finite quadrature garbage.
    EXPECT_EQ(sm::ellint_3_fallback(1.0, 0.5, 2.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::ellint_3_fallback(-1.0, -2.0, -2.0),
              -std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::comp_ellint_3_fallback(1.0, 0.25),
              std::numeric_limits<double>::infinity());
    // Below the pole Pi stays finite; nu = 0 reduces Pi to F.
    EXPECT_NEAR(sm::ellint_3_fallback(1.0, 0.0, 1.0),
                std::asinh(std::tan(1.0)), 1e-9);

    // General nu-pole handling, independent of k. For nu > 1 the factor
    // 1 - nu sin^2 changes sign at asin(1/sqrt(nu)) < pi/2: crossing it is
    // a domain error (NaN), not a finite quadrature artifact.
    EXPECT_TRUE(std::isnan(sm::ellint_3_fallback(0.0, 2.0, 1.0)));
    EXPECT_TRUE(std::isnan(sm::comp_ellint_3_fallback(0.0, 4.0)));
    // nu == 1 diverges at pi/2 through the second-order cos^2 pole even
    // for |k| < 1.
    EXPECT_EQ(sm::ellint_3_fallback(0.5, 1.0, 2.0),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ(sm::ellint_3_fallback(0.0, 1.0, -2.0),
              -std::numeric_limits<double>::infinity());
    // Inside the pole the integral stays finite:
    // Pi(nu = 2, phi, k = 0) = (1/2) ln(sec 2phi + tan 2phi).
    EXPECT_NEAR(sm::ellint_3_fallback(0.0, 2.0, 0.5),
                0.5 * std::log(1.0 / std::cos(1.0) + std::tan(1.0)), 1e-9);
}

TEST(SimdMathSpecialTest, Ellint3ResolvesTheCharacteristicPoleBoundary) {
    namespace sm = std::simd::detail::special_math;
    constexpr double half_pi = std::numbers::pi_v<double> / 2.0;

    const double below_one = std::nextafter(1.0, 0.0);
    const double complete_expected = static_cast<double>(
        std::numbers::pi_v<long double> /
        (2.0L * std::sqrt(
            1.0L - static_cast<long double>(below_one))));
    const double complete =
        sm::comp_ellint_3_fallback(0.0, below_one);
    EXPECT_TRUE(std::isfinite(complete));
    EXPECT_NEAR(
        complete,
        complete_expected,
        complete_expected * 2.0e-12);
    EXPECT_EQ(
        sm::comp_ellint_3_fallback(0.0, 1.0),
        std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(sm::comp_ellint_3_fallback(
        0.0, std::nextafter(1.0, 2.0))));

    constexpr double nu = 2.0;
    const double pole = std::asin(1.0 / std::sqrt(nu));
    const double below_pole = std::nextafter(pole, 0.0);
    const long double scale = std::sqrt(
        static_cast<long double>(nu) - 1.0L);
    const double incomplete_expected = static_cast<double>(
        std::atanh(
            scale * std::tan(static_cast<long double>(below_pole))) /
        scale);
    const double incomplete =
        sm::ellint_3_fallback(0.0, nu, below_pole);
    EXPECT_TRUE(std::isfinite(incomplete));
    EXPECT_NEAR(
        incomplete,
        incomplete_expected,
        incomplete_expected * 2.0e-12);
    EXPECT_EQ(
        sm::ellint_3_fallback(0.0, nu, pole),
        std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(sm::ellint_3_fallback(
        0.0, nu, std::nextafter(pole, 1.0))));

    struct complete_case {
        double characteristic;
        double expected;
    };
    const complete_case nonzero_modulus_cases[] = {
        {1.0 - 1.0e-12, 1813819.1558824056},
        {std::nextafter(1.0, 0.0), 172140923.98024535},
    };
    for (const auto& value : nonzero_modulus_cases) {
        const double actual =
            sm::comp_ellint_3_fallback(0.5, value.characteristic);
        EXPECT_NEAR(actual, value.expected, value.expected * 2.0e-8);
    }
}

TEST(SimdMathSpecialTest, SphericalLegendreNormalizesBeforeFloatOverflow) {
    namespace sm = std::simd::detail::special_math;

    for (const unsigned degree : {30u, 35u, 64u, 100u, 127u}) {
        const long double degree_value = static_cast<long double>(degree);
        const long double log_magnitude =
            0.5L * std::log(
                (2.0L * degree_value + 1.0L) /
                (4.0L * std::numbers::pi_v<long double>)) +
            0.5L * std::lgamma(2.0L * degree_value + 1.0L) -
            degree_value * std::log(2.0L) -
            std::lgamma(degree_value + 1.0L);
        const long double magnitude = std::exp(log_magnitude);
        const float expected = static_cast<float>(
            (degree & 1u) == 0u ? magnitude : -magnitude);
        const float actual = sm::sph_legendre_fallback(
            degree,
            degree,
            std::numbers::pi_v<float> / 2.0f);

        SCOPED_TRACE("degree=" + std::to_string(degree));
        EXPECT_TRUE(std::isfinite(actual));
        EXPECT_NEAR(actual, expected, std::abs(expected) * 2e-6f);
    }
}

TEST(SimdMathSpecialTest, PolynomialFallbacksPreserveNaNAndSmallAngles) {
    namespace sm = std::simd::detail::special_math;

    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isnan(sm::hermite_fallback(0u, nan)));
    EXPECT_TRUE(std::isnan(sm::laguerre_fallback(0u, nan)));
    EXPECT_TRUE(std::isnan(sm::legendre_fallback(0u, nan)));
    EXPECT_TRUE(std::isnan(sm::assoc_laguerre_fallback(0u, 0u, nan)));
    EXPECT_TRUE(std::isnan(sm::assoc_legendre_fallback(0u, 0u, nan)));
    EXPECT_TRUE(std::isnan(sm::sph_legendre_fallback(0u, 0u, nan)));

    constexpr double theta = 1.0e-12;
    const double expected =
        -std::sqrt(3.0 / (8.0 * std::numbers::pi)) * std::sin(theta);
    EXPECT_NEAR(sm::sph_legendre_fallback(1u, 1u, theta),
                expected,
                std::abs(expected) * 2e-15);
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
TEST(SimdMathSpecialTest, RiemannZetaPreservesNearPoleAndLargeNegativeInputs) {
    const double4 values = load_vec<double4>(std::array<double, 4>{{
        1.0 + 1.0e-13,
        1.0 - 1.0e-13,
        -1.0e30,
        -2.0}});
    const auto zeta = std::simd::riemann_zeta(values);

    EXPECT_TRUE(std::isfinite(zeta[0]));
    EXPECT_TRUE(std::isfinite(zeta[1]));
    EXPECT_GT(zeta[0], 1.0e12);
    EXPECT_LT(zeta[1], -1.0e12);
    EXPECT_EQ(zeta[2], 0.0);
    EXPECT_EQ(zeta[3], 0.0);
}

TEST(SimdMathSpecialTest, RiemannZetaNearPoleUsesStableLocalExpansion) {
    namespace sm = std::simd::detail::special_math;

    constexpr long double euler_gamma =
        0.577215664901532860606512090082402431L;
    constexpr long double minus_stieltjes_one =
        0.072815845483676724860586375874901319L;
    for (const double requested_delta : {1.0e-8, -1.0e-8}) {
        const double argument = 1.0 + requested_delta;
        const long double delta =
            static_cast<long double>(argument) - 1.0L;
        const long double expected =
            1.0L / delta + euler_gamma + minus_stieltjes_one * delta;
        const double actual = sm::riemann_zeta_fallback(argument);
        EXPECT_TRUE(std::isfinite(actual));
        EXPECT_NEAR(
            actual,
            static_cast<double>(expected),
            std::abs(static_cast<double>(expected)) * 5.0e-15);
    }

    EXPECT_TRUE(std::isfinite(sm::riemann_zeta_fallback(
        std::nextafter(1.0, 2.0))));
    EXPECT_TRUE(std::isfinite(sm::riemann_zeta_fallback(
        std::nextafter(1.0, 0.0))));
}

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

TEST(SimdMathSpecialTest, ExpintFallbackAvoidsRepresentableOverflowBand) {
    namespace sm = std::simd::detail::special_math;

    constexpr double expected = 3.1509156882062014e305;
    EXPECT_NEAR(sm::expint_fallback(710.0),
                expected,
                expected * 3e-14);
    EXPECT_EQ(sm::expint_fallback(
                  std::numeric_limits<double>::infinity()),
              std::numeric_limits<double>::infinity());
    const double negative_limit = sm::expint_fallback(
        -std::numeric_limits<double>::infinity());
    EXPECT_EQ(negative_limit, 0.0);
    EXPECT_TRUE(std::signbit(negative_limit));
}

TEST(SimdMathSpecialTest, ZetaReflectionAvoidsIntermediateOverflow) {
    namespace sm = std::simd::detail::special_math;

    struct reference {
        double argument;
        double value;
    };
    constexpr reference cases[] = {
        {-170.5, 1.7363215609094486e171},
        {-180.5, -5.1567927348836793e185},
        {-200.5, -2.3200006633528888e215},
    };
    for (const auto& value : cases) {
        SCOPED_TRACE("argument=" + std::to_string(value.argument));
        EXPECT_NEAR(sm::riemann_zeta_fallback(value.argument),
                    value.value,
                    std::abs(value.value) * 5e-13);
    }

    constexpr float expected_float = 6.424299552063126e11f;
    EXPECT_NEAR(sm::riemann_zeta_fallback(-35.5f),
                expected_float,
                std::abs(expected_float) * 2e-6f);
}

TEST(SimdMathSpecialTest, ZetaNearZeroMatchesIndependentReferences) {
    namespace sm = std::simd::detail::special_math;
    struct reference {
        long double argument;
        long double value;
    };
    // Independently evaluated with 90 decimal digits, not the local expansion.
    constexpr reference cases[] = {
        {-1.0e-5L, -0.499990810714984775292815655216544598302L},
        {-1.0e-8L, -0.499999990810614768271094376840760718199L},
        {-1.0e-12L, -0.499999999999081061466796330436447623555L},
        {-1.0e-17L, -0.499999999999999990810614667953272682515L},
        {-1.0e-30L, -0.499999999999999999999999999999081061467L},
        {1.0e-5L, -0.500009189485650870318040415896476830273L},
        {1.0e-8L, -0.500000009189385432364551214017744400397L},
        {1.0e-12L, -0.500000000000918938533205675920008285030L},
    };
    const auto check = [&]<class T>() {
        for (const auto& test_case : cases) {
            SCOPED_TRACE(test_case.argument);
            const T actual = sm::riemann_zeta_fallback(
                static_cast<T>(test_case.argument));
            const T expected = static_cast<T>(test_case.value);
            const long double magnitude = std::abs(test_case.argument);
            const T truncation_budget = static_cast<T>(
                2e-4L * magnitude * magnitude * magnitude * magnitude);
            EXPECT_TRUE(std::isfinite(actual));
            EXPECT_NEAR(actual, expected,
                        std::numeric_limits<T>::epsilon() * T{16} + truncation_budget);
        }
        EXPECT_EQ(sm::riemann_zeta_fallback(T{}), T{-0.5});
        EXPECT_EQ(sm::riemann_zeta_fallback(-T{}), T{-0.5});
        for (const T magnitude : {
                 std::numeric_limits<T>::denorm_min(),
                 std::numeric_limits<T>::min(),
                 std::numeric_limits<T>::epsilon() / T{16}}) {
            for (const T sign : {T{-1}, T{1}}) {
                const T actual = sm::riemann_zeta_fallback(sign * magnitude);
                EXPECT_TRUE(std::isfinite(actual));
                EXPECT_NEAR(actual, T{-0.5},
                            std::numeric_limits<T>::epsilon() * T{16});
            }
        }
    };
    check.template operator()<float>();
    check.template operator()<double>();
    check.template operator()<long double>();
}

TEST(SimdMathSpecialTest, ZetaNearZeroBranchBoundaryIsContinuous) {
    namespace sm = std::simd::detail::special_math;
    const auto check = []<class T>() {
        const T boundary = static_cast<T>(1e-5L);
        using wide_t = std::conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
        const T tolerance = std::max({
            std::numeric_limits<T>::epsilon() * T{16},
            static_cast<T>(5e-14L),
            static_cast<T>(4 * std::numeric_limits<wide_t>::epsilon()) / boundary});
        for (const T sign : {T{-1}, T{1}}) {
            const T expected = static_cast<T>(sign < T{}
                ? -0.499990810714984775292815655216544598302L
                : -0.500009189485650870318040415896476830273L);
            const T endpoint = sign * boundary;
            for (const T argument : {
                     std::nextafter(endpoint, T{}), endpoint,
                     std::nextafter(endpoint, sign * std::numeric_limits<T>::infinity())}) {
                const T actual = sm::riemann_zeta_fallback(argument);
                EXPECT_TRUE(std::isfinite(actual));
                EXPECT_NEAR(actual, expected, tolerance);
            }
        }
    };
    check.template operator()<float>();
    check.template operator()<double>();
    check.template operator()<long double>();
}

#if !defined(__cpp_lib_math_special_functions)
TEST(SimdMathSpecialTest, PublicZetaFallbackStaysFiniteNearZero) {
    const double4 arguments = load_vec<double4>(std::array<double, 4>{{
        -1.0e-14, -1.0e-17, -1.0e-30, -std::numeric_limits<double>::denorm_min()}});
    const auto values = std::simd::riemann_zeta(arguments);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(std::isfinite(values[i]));
        EXPECT_NEAR(values[i], -0.5, 2e-14);
    }
}
#endif

TEST(SimdMathSpecialTest, ExpintFloatFallbackAvoidsNegativeSeriesCancellation) {
    const float actual =
        std::simd::detail::special_math::expint_fallback(-6.0f);
    constexpr float expected = -0.000360082452163f;

    EXPECT_NEAR(actual, expected, std::abs(expected) * 8e-7f);
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
    const std::pair<double, double> cases[] = {
        {0.3137, 1.0e-8},
        {0.49, 0.01},
        {0.2, 0.1},
        {2.5, 3.0},
        {0.9, 16.1},
        {20.0, 20.0},
        {50.0, 49.5},
        {100.0, 101.0},
        {std::nextafter(1.0, 2.0), 2.0},
        {1.0e-17, 10.0},
        {1.0 + 1.0e-12, 2.0},
        {2.0 + 1.0e-8, 3.0},
        {20.0 + 1.0e-5, 10.0},
        {-0.5, 1.0},
        {-1.25, 2.0},
        {-1.0 + 1.0e-12, 2.0},
        {-10.5, 20.0},
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

        EXPECT_TRUE(current.converged);
        EXPECT_TRUE(next.converged);
        EXPECT_NEAR(
            actual,
            expected,
            std::max(2e-14, std::abs(expected) * 2e-12));
    }
}

TEST(SimdMathSpecialTest, SphericalBesselFallbackCoversTurningBand) {
    namespace sm = std::simd::detail::special_math;

    const double j100 = sm::sph_bessel_fallback(100u, 100.0);
    const double j127 = sm::sph_bessel_fallback(127u, 128.0);
    const float j50 = sm::sph_bessel_fallback(50u, 50.0f);

    EXPECT_LE(std::abs(j100), 1.0);
    EXPECT_NEAR(j127, 0.01073050471, 2e-11);
    EXPECT_NEAR(j50, 0.0188290642f, 5e-7f);

    constexpr unsigned orders[] = {15u, 30u, 50u, 75u, 100u, 125u, 126u};
    for (const unsigned order : orders) {
        const double argument = static_cast<double>(order);
        const double previous =
            sm::sph_bessel_fallback(order - 1u, argument);
        const double current =
            sm::sph_bessel_fallback(order, argument);
        const double next =
            sm::sph_bessel_fallback(order + 1u, argument);
        const double expected =
            (2.0 * static_cast<double>(order) + 1.0) /
            argument * current;
        const double actual = previous + next;
        const double scale = std::max({
            std::abs(actual), std::abs(expected), 1e-300});

        SCOPED_TRACE("order=" + std::to_string(order));
        EXPECT_LE(std::abs(current), 1.0);
        EXPECT_NEAR(actual, expected, scale * 2e-11);
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

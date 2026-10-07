#ifndef FORGE_SM04_PRIVATE_KERNEL_PROBE
#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <type_traits>

#ifdef FORGE_SM04_PRIVATE_KERNEL_PROBE
namespace std::simd {
#include "common.hpp"
#include "elliptic.hpp"
}
static_assert(std::numeric_limits<long double>::digits == 53);
#endif

namespace {

namespace math = std::simd::detail::special_math;
struct reference {
    double modulus;
    double characteristic;
    double angle;
    long double value;
};

// mpmath 1.3.0, exact binary inputs, independently at 100 and 160 digits.
// The 1e300 characteristic was checked at 600 and 800 digits instead.
constexpr reference double_references[] = {
    {0x1.fffffffffffffp-1, 0x1.fffffffffffffp-1, 0x1.6666666666666p+0,
        18.2848934716991606789968860289739283682541614L},
    {0x1.fffffffffffffp-1, 0x1.fffffffffffffp-1, 0x1.921fb54442d17p+0,
        7074237580798043.59488536574668010625279586017L},
    {0x1.fffffffffffffp-1, 0x1.fffffffffffffp-1, 0x1.921fb54442d19p+0,
        7074237849233499.59488531219485886235398273884L},
    {0x1p-1, -0x1.d1a94a2p+39, 0x1.6666666666666p-1,
        0.00000157079522962025791268892810020557054350204921L},
    {0x1.ccccccccccccdp-1, -0x1.6bcc41e9p+46, 0x1.6666666666666p+0,
        0.000000157079639889029626380722599956131618880184824L},
    {0x1.ccccccccccccdp-1, -0x1.6bcc41e9p+46, 0x1.921fb54442d18p+0,
        0.000000157079643768009096763944655028852387382102294L},
    {0x1.ccccccccccccdp-1, -0x1.6bcc41e9p+46, 0x1.4p+3,
        0.00000109955748236089716679374445917283500665866129L},
    {0x1p-1, -0x1.7e43c8800759cp+996, 0x1p+0,
        1.57079632679489657799417941757886200293926309e-150L},
    {0x1.6666666666666p-1, 0x1.4p+0, 0x1.1b6e192ebbe43p+0,
        45.1235333448072209431697084782356837604243779L},
    {0x1.6666666666666p-1, 0x1p+1, 0x1.921fb54366ea7p-1,
        13.1220528247900928758760336006053897587528208L},
    {0x1.6666666666666p-1, 0x1.d1a94a2p+39, 0x1.0c6f7a0b5f09fp-20,
        0.0000182707313852880139572479566270360999104315423L},
    {0x1p-1, 0x0p+0, 0x1.6666666666666p+0,
        1.4888481322276381491368011828950735570536097L},
    {1e-200, -1e-320, 1.0, 1.0L},
};

constexpr reference float_references[] = {
    {0x1p-1, 0x1.fffffep-1, 0x1.666666p+0,
        6.45673886192373848519227023850062009151564975L},
    {0x1.ccccccp-1, -0x1.6bcc42p+46, 0x1.666666p+0,
        0.000000157079639593065002076460991921903170862241698L},
    {0x1.666666p-1, 0x1.4p+0, 0x1.1b6e16p+0,
        19.2013455950479034310169935985006035660472825L},
};

// mpmath 1.4.1 ellippi, exact binary inputs, checked at 600 and 800 digits.
// The complete-endpoint asymptotic is independently bounded by k^2 K(k)/a;
// the typed half-pi versus exact pi/2 tail is below the reference budget.
constexpr reference subnormal_references[] = {
    {0x1.fffffffffffffp-1, -0x1.8p+1020, 0x1.921fb54442d18p+0,
        3.826277455236055372515059789914689685240903468163155869e-154L},
    {0x1.fffffffffffffp-1, -0x1p+970, 0x1.921fb54442d18p+0,
        1.572432385925886156581211730393126533487984810988248079e-146L},
    {0x1.fffffffffffffp-1, -0x1p+971, 0x1.921fb54442d18p+0,
        1.111877603045536433634789758463292561569090402409896836e-146L},
    {0x1.fffffffffffffp-1, -0x1.fffffffffffffp+1023, 0x1.921fb54442d18p+0,
        1.171553422455404880545097370782920535934551417894392186e-154L},
    {0x1p-1, -0x1.fffffffffffffp+1023, 0x1.921fb54442d18p+0,
        1.171553422455404880545097370782920535934551417894392186e-154L},
    {0x1.fffffffffffffp-1, -0x1.8p+1020, 0x1p+0,
        3.826277455236055372515059789914689685240903468163155869e-154L},
    {0x1.fffffffffffffp-1, -0x1.8p+1020, 0x1.4p+3,
        2.678394218665238760760541852940282779668632427714209108e-153L},
};

template<class T, std::size_t N>
unsigned check_references(const reference (&values)[N], long double tolerance) {
    unsigned failures = 0;
    for (const auto& input : values) {
        const T k = static_cast<T>(input.modulus);
        const T n = static_cast<T>(input.characteristic);
        const T phi = static_cast<T>(input.angle);
        const T actual = math::ellint_3_fallback(k, n, phi);
        const long double error = std::abs(static_cast<long double>(actual) - input.value);
        const bool valid = std::isfinite(actual) &&
            error <= tolerance * std::abs(input.value) &&
            math::ellint_3_fallback(-k, n, phi) == actual &&
            math::ellint_3_fallback(k, n, -phi) == -actual;
        if (!valid) {
            ++failures;
        }
#ifndef FORGE_SM04_PRIVATE_KERNEL_PROBE
        SCOPED_TRACE(input.modulus);
        SCOPED_TRACE(input.characteristic);
        SCOPED_TRACE(input.angle);
        EXPECT_TRUE(valid);
#endif
    }
    return failures;
}

#ifndef FORGE_SM04_PRIVATE_KERNEL_PROBE
TEST(SimdMathIncompleteElliptic, DoubleCharacteristicExtremesKeepIndependentReferences) {
    EXPECT_EQ(check_references<double>(double_references, 3e-12L), 0u);
}

TEST(SimdMathIncompleteElliptic, FloatCharacteristicExtremesKeepIndependentReferences) {
    EXPECT_EQ(check_references<float>(float_references, 8e-6L), 0u);
}

TEST(SimdMathIncompleteElliptic, NegativeSubnormalComplementsKeepExactInputReferences) {
    EXPECT_EQ(check_references<double>(subnormal_references, 3e-12L), 0u);
}

TEST(SimdMathIncompleteElliptic, CompleteAndPeriodControlsStayConsistent) {
    for (const double modulus : {0.125, 0.5, 0.9, std::nextafter(1.0, 0.0)}) {
        for (const double characteristic : {-1e14, -2.0, 0.0, 0.5,
                std::nextafter(1.0, 0.0)}) {
            const double half_pi = math::pi_v<double> / 2;
            const double complete = math::comp_ellint_3_fallback(modulus, characteristic);
            EXPECT_NEAR(math::ellint_3_fallback(modulus, characteristic, half_pi),
                complete, std::abs(complete) * 3e-12);
            const double principal = math::ellint_3_fallback(modulus, characteristic, 0.75);
            EXPECT_NEAR(math::ellint_3_fallback(modulus, characteristic,
                math::pi_v<double> + 0.75), principal + 2 * complete,
                std::abs(principal + 2 * complete) * 3e-12);
        }
    }
    EXPECT_TRUE(std::isnan(math::ellint_3_fallback(0.5, 2.0, 1.0)));
    EXPECT_TRUE(std::signbit(math::ellint_3_fallback(0.5, 0.5, -0.0)));
    EXPECT_EQ(math::ellint_3_fallback(0.5, 0.5,
        std::numeric_limits<double>::infinity()), std::numeric_limits<double>::infinity());
}
#endif

} // namespace

#ifdef FORGE_SM04_PRIVATE_KERNEL_PROBE
int main() {
    const unsigned failures = check_references<double>(double_references, 3e-12L) +
        check_references<float>(float_references, 8e-6L) +
        check_references<double>(subnormal_references, 3e-12L);
    constexpr auto count = sizeof(double_references) / sizeof(reference) +
        sizeof(float_references) / sizeof(reference) +
        sizeof(subnormal_references) / sizeof(reference);
    std::printf("SM04 private-kernel references: %zu checked, %u failures\n", count, failures);
    return failures != 0;
}
#endif

#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

static_assert(std::is_same_v<decltype(std::simd::comp_ellint_1(std::declval<const float4&>())), float4>,
    "comp_ellint_1 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::comp_ellint_2(std::declval<const float4&>())), float4>,
    "comp_ellint_2 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::expint(std::declval<const float4&>())), float4>,
    "expint should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::riemann_zeta(std::declval<const float4&>())), float4>,
    "riemann_zeta should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::beta(std::declval<const float4&>(), 0.5f)), float4>,
    "beta(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::comp_ellint_3(std::declval<const float4&>(), 0.5f)), float4>,
    "comp_ellint_3(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::cyl_bessel_i(std::declval<const float4&>(), 0.5f)), float4>,
    "cyl_bessel_i(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::cyl_bessel_j(std::declval<const float4&>(), 0.5f)), float4>,
    "cyl_bessel_j(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::cyl_bessel_k(std::declval<const float4&>(), 0.5f)), float4>,
    "cyl_bessel_k(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::cyl_neumann(std::declval<const float4&>(), 0.5f)), float4>,
    "cyl_neumann(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::ellint_1(std::declval<const float4&>(), 0.5f)), float4>,
    "ellint_1(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::ellint_2(std::declval<const float4&>(), 0.5f)), float4>,
    "ellint_2(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::ellint_3(std::declval<const float4&>(), 0.5f, 0.25f)), float4>,
    "ellint_3(vec, scalar, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::hermite(std::declval<const uint4&>(), std::declval<const float4&>())), float4>,
    "hermite(order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::laguerre(std::declval<const uint4&>(), std::declval<const float4&>())), float4>,
    "laguerre(order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::legendre(std::declval<const uint4&>(), std::declval<const float4&>())), float4>,
    "legendre(order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::sph_bessel(std::declval<const uint4&>(), std::declval<const float4&>())), float4>,
    "sph_bessel(order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::sph_neumann(std::declval<const uint4&>(), std::declval<const float4&>())), float4>,
    "sph_neumann(order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::assoc_laguerre(std::declval<const uint4&>(),
                                                                std::declval<const uint4&>(),
                                                                std::declval<const float4&>())), float4>,
    "assoc_laguerre(order_vec, order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::assoc_legendre(std::declval<const uint4&>(),
                                                                std::declval<const uint4&>(),
                                                                std::declval<const float4&>())), float4>,
    "assoc_legendre(order_vec, order_vec, vec) should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::sph_legendre(std::declval<const uint4&>(),
                                                              std::declval<const uint4&>(),
                                                              std::declval<const float4&>())), float4>,
    "sph_legendre(order_vec, order_vec, vec) should preserve the floating vector type");

} // namespace

int main() {
    return 0;
}

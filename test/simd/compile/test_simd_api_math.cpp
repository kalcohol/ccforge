#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

template<class V, class = void>
struct has_sin : std::false_type {};

template<class V>
struct has_sin<V, std::void_t<decltype(std::simd::sin(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_sqrt : std::false_type {};

template<class V>
struct has_sqrt<V, std::void_t<decltype(std::simd::sqrt(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_floor : std::false_type {};

template<class V>
struct has_floor<V, std::void_t<decltype(std::simd::floor(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_abs : std::false_type {};

template<class V>
struct has_abs<V, std::void_t<decltype(std::simd::abs(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_bit_floor : std::false_type {};

template<class V>
struct has_bit_floor<V, std::void_t<decltype(std::simd::bit_floor(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_pair_frexp : std::false_type {};

template<class V>
struct has_pair_frexp<V, std::void_t<decltype(std::simd::frexp(std::declval<const V&>()))>> : std::true_type {};

template<class V, class = void>
struct has_pair_modf : std::false_type {};

template<class V>
struct has_pair_modf<V, std::void_t<decltype(std::simd::modf(std::declval<const V&>()))>> : std::true_type {};

template<class A, class B, class = void>
struct has_pair_remquo : std::false_type {};

template<class A, class B>
struct has_pair_remquo<A, B, std::void_t<decltype(std::simd::remquo(std::declval<const A&>(), std::declval<const B&>()))>>
    : std::true_type {};

static_assert(std::is_same_v<decltype(std::simd::byteswap(std::declval<const uint4&>())), uint4>,
    "byteswap should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::popcount(std::declval<const uint4&>())), int4>,
    "popcount(uint4) should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::popcount(std::declval<const uchar4&>())), schar4>,
    "popcount(uchar4) should return rebind_t<signed char, V>");
static_assert(std::is_same_v<decltype(std::simd::countl_zero(std::declval<const uint4&>())), int4>,
    "countl_zero(uint4) should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::countl_zero(std::declval<const uchar4&>())), schar4>,
    "countl_zero(uchar4) should return rebind_t<signed char, V>");
static_assert(std::is_same_v<decltype(std::simd::countl_one(std::declval<const uint4&>())), int4>,
    "countl_one should return the signed rebound type");
static_assert(std::is_same_v<decltype(std::simd::countr_zero(std::declval<const uint4&>())), int4>,
    "countr_zero should return the signed rebound type");
static_assert(std::is_same_v<decltype(std::simd::countr_one(std::declval<const uint4&>())), int4>,
    "countr_one should return the signed rebound type");
static_assert(std::is_same_v<decltype(std::simd::bit_width(std::declval<const uint4&>())), int4>,
    "bit_width should return the signed rebound type");
static_assert(std::is_same_v<decltype(std::simd::has_single_bit(std::declval<const uint4&>())), uint_mask4>,
    "has_single_bit should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::bit_floor(std::declval<const uint4&>())), uint4>,
    "bit_floor should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::bit_ceil(std::declval<const uint4&>())), uint4>,
    "bit_ceil should preserve the vector type");
static_assert(noexcept(std::simd::bit_floor(std::declval<const uint4&>())),
    "bit_floor should be noexcept");
static_assert(!noexcept(std::simd::bit_ceil(std::declval<const uint4&>())),
    "bit_ceil should not be noexcept");
static_assert(std::is_same_v<decltype(std::simd::rotl(std::declval<const uint4&>(), 1)), uint4>,
    "rotl(vec, scalar) should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotl(std::declval<const uint4&>(), std::declval<const int4&>())), uint4>,
    "rotl(unsigned vec, signed vec) should preserve the value vector type");
static_assert(std::is_same_v<decltype(std::simd::rotr(std::declval<const uint4&>(), 1)), uint4>,
    "rotr(vec, scalar) should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotr(std::declval<const uint4&>(), std::declval<const int4&>())), uint4>,
    "rotr(unsigned vec, signed vec) should preserve the value vector type");

static_assert(std::is_same_v<decltype(std::simd::fabs(std::declval<const float4&>())), float4>,
    "fabs should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::tan(std::declval<const float4&>())), float4>,
    "tan should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::log10(std::declval<const float4&>())), float4>,
    "log10 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::log1p(std::declval<const float4&>())), float4>,
    "log1p should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::log2(std::declval<const float4&>())), float4>,
    "log2 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::logb(std::declval<const float4&>())), float4>,
    "logb should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::exp2(std::declval<const float4&>())), float4>,
    "exp2 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::expm1(std::declval<const float4&>())), float4>,
    "expm1 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::cbrt(std::declval<const float4&>())), float4>,
    "cbrt should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::erf(std::declval<const float4&>())), float4>,
    "erf should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::erfc(std::declval<const float4&>())), float4>,
    "erfc should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::lgamma(std::declval<const float4&>())), float4>,
    "lgamma should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::tgamma(std::declval<const float4&>())), float4>,
    "tgamma should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::pow(std::declval<const float4&>(), 2.0f)), float4>,
    "pow(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::pow(2.0f, std::declval<const float4&>())), float4>,
    "pow(scalar, vec) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::atan2(std::declval<const float4&>(), 2.0f)), float4>,
    "atan2(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::hypot(std::declval<const float4&>(), 2.0f)), float4>,
    "hypot(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::hypot(std::declval<const float4&>(), 2.0f, 3.0f)), float4>,
    "hypot(vec, scalar, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::fmin(std::declval<const float4&>(), std::declval<const double4&>())), double4>,
    "mixed floating arguments should widen to the common vector type");
static_assert(std::is_same_v<decltype(std::simd::fmax(std::declval<const float4&>(), 2.0f)), float4>,
    "fmax(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::copysign(std::declval<const float4&>(), 2.0f)), float4>,
    "copysign(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::nextafter(std::declval<const float4&>(), 2.0f)), float4>,
    "nextafter(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::fdim(std::declval<const float4&>(), 2.0f)), float4>,
    "fdim(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::fma(std::declval<const float4&>(), 2.0f, 3.0f)), float4>,
    "fma(vec, scalar, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::lerp(std::declval<const float4&>(), 2.0f, 3.0f)), float4>,
    "lerp(vec, scalar, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::ilogb(std::declval<const float4&>())), int4>,
    "ilogb should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::ldexp(std::declval<const float4&>(), std::declval<const int4&>())), float4>,
    "ldexp should preserve the floating vector type");
static_assert(!noexcept(std::simd::lrint(std::declval<const float4&>())),
    "lrint should not be noexcept");
static_assert(!noexcept(std::simd::llrint(std::declval<const float4&>())),
    "llrint should not be noexcept");
static_assert(std::is_same_v<decltype(std::simd::scalbn(std::declval<const float4&>(), std::declval<const int4&>())), float4>,
    "scalbn should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::scalbln(std::declval<const float4&>(), std::declval<const long4&>())), float4>,
    "scalbln should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::fpclassify(std::declval<const float4&>())), int4>,
    "fpclassify should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::isfinite(std::declval<const float4&>())), typename float4::mask_type>,
    "isfinite should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::isinf(std::declval<const float4&>())), typename float4::mask_type>,
    "isinf should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::isnan(std::declval<const float4&>())), typename float4::mask_type>,
    "isnan should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::isnormal(std::declval<const float4&>())), typename float4::mask_type>,
    "isnormal should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::signbit(std::declval<const float4&>())), typename float4::mask_type>,
    "signbit should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::isgreater(std::declval<const float4&>(), 0.0f)), typename float4::mask_type>,
    "isgreater should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::isunordered(std::declval<const float4&>(), 0.0f)), typename float4::mask_type>,
    "isunordered should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::frexp(std::declval<const float4&>(), std::declval<int4*>())), float4>,
    "frexp should use the standard out-parameter signature");
static_assert(std::is_same_v<decltype(std::simd::modf(std::declval<const float4&>(), std::declval<float4*>())), float4>,
    "modf should use the standard out-parameter signature");
static_assert(std::is_same_v<decltype(std::simd::remquo(std::declval<const float4&>(), 2.0f, std::declval<int4*>())), float4>,
    "remquo should use the standard out-parameter signature");
static_assert(!has_pair_frexp<float4>::value,
    "frexp should not expose the removed pair-return signature");
static_assert(!has_pair_modf<float4>::value,
    "modf should not expose the removed pair-return signature");
static_assert(!has_pair_remquo<float4, float>::value,
    "remquo should not expose the removed pair-return signature");

static_assert(!has_sin<int4>::value,
    "integral vectors should not expose sin");
static_assert(!has_sqrt<int4>::value,
    "integral vectors should not expose sqrt");
static_assert(!has_floor<int4>::value,
    "integral vectors should not expose floor");
static_assert(!has_abs<uint4>::value,
    "unsigned vectors should not expose abs");
static_assert(!has_bit_floor<int4>::value,
    "signed vectors should not expose unsigned-only bit floor");

} // namespace

int main() {
    return 0;
}

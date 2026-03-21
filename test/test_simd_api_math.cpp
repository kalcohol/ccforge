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

static_assert(std::is_same_v<decltype(std::simd::byteswap(std::declval<const uint4&>())), uint4>,
    "byteswap should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::popcount(std::declval<const uint4&>())), int4>,
    "popcount should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::countl_zero(std::declval<const uint4&>())), int4>,
    "countl_zero should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::countl_one(std::declval<const uint4&>())), int4>,
    "countl_one should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::countr_zero(std::declval<const uint4&>())), int4>,
    "countr_zero should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::countr_one(std::declval<const uint4&>())), int4>,
    "countr_one should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::bit_width(std::declval<const uint4&>())), int4>,
    "bit_width should return rebind_t<int, V>");
static_assert(std::is_same_v<decltype(std::simd::has_single_bit(std::declval<const uint4&>())), uint_mask4>,
    "has_single_bit should return the vector mask type");
static_assert(std::is_same_v<decltype(std::simd::bit_floor(std::declval<const uint4&>())), uint4>,
    "bit_floor should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::bit_ceil(std::declval<const uint4&>())), uint4>,
    "bit_ceil should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotl(std::declval<const uint4&>(), std::simd::simd_size_type{})), uint4>,
    "rotl(vec, scalar) should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotl(std::declval<const uint4&>(), std::declval<const uint4&>())), uint4>,
    "rotl(vec, vec) should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotr(std::declval<const uint4&>(), std::simd::simd_size_type{})), uint4>,
    "rotr(vec, scalar) should preserve the vector type");
static_assert(std::is_same_v<decltype(std::simd::rotr(std::declval<const uint4&>(), std::declval<const uint4&>())), uint4>,
    "rotr(vec, vec) should preserve the vector type");

static_assert(std::is_same_v<decltype(std::simd::log10(std::declval<const float4&>())), float4>,
    "log10 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::log1p(std::declval<const float4&>())), float4>,
    "log1p should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::log2(std::declval<const float4&>())), float4>,
    "log2 should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::logb(std::declval<const float4&>())), float4>,
    "logb should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::cbrt(std::declval<const float4&>())), float4>,
    "cbrt should preserve the floating vector type");
static_assert(std::is_same_v<decltype(std::simd::pow(std::declval<const float4&>(), 2.0f)), float4>,
    "pow(vec, scalar) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::pow(2.0f, std::declval<const float4&>())), float4>,
    "pow(scalar, vec) should preserve the vector width");
static_assert(std::is_same_v<decltype(std::simd::hypot(std::declval<const float4&>(), 2.0f)), float4>,
    "hypot(vec, scalar) should preserve the vector width");
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
static_assert(std::is_same_v<decltype(std::simd::frexp(std::declval<const float4&>())), std::pair<float4, int4>>,
    "frexp should return pair<vec, rebind_t<int, vec>>");
static_assert(std::is_same_v<decltype(std::simd::modf(std::declval<const float4&>())), std::pair<float4, float4>>,
    "modf should return pair<vec, vec>");
static_assert(std::is_same_v<decltype(std::simd::remquo(std::declval<const float4&>(), 2.0f)), std::pair<float4, int4>>,
    "remquo should return pair<vec, rebind_t<int, vec>>");

static_assert(!has_sin<int4>::value,
    "integral vectors should not expose sin");
static_assert(!has_sqrt<int4>::value,
    "integral vectors should not expose sqrt");
static_assert(!has_floor<int4>::value,
    "integral vectors should not expose floor");
static_assert(!has_abs<uint4>::value,
    "unsigned vectors should not expose abs");

} // namespace

int main() {
    return 0;
}

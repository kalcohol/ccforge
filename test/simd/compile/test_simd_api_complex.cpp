#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

template<class V, class = void>
struct has_less : std::false_type {};

template<class V>
struct has_less<V, std::void_t<decltype(std::declval<const V&>() < std::declval<const V&>())>> : std::true_type {};

template<class A, class B, class = void>
struct has_pow : std::false_type {};

template<class A, class B>
struct has_pow<A, B, std::void_t<decltype(std::simd::pow(std::declval<const A&>(), std::declval<const B&>()))>>
    : std::true_type {};

static_assert(std::is_same_v<typename complex4f::value_type, std::complex<float>>,
    "complex4f should expose complex<float> as value_type");
static_assert(std::is_same_v<typename complex4f::real_type, float4>,
    "complex simd should expose a matching real_type");
static_assert(std::is_constructible_v<complex4f, float4, float4>,
    "complex simd should be constructible from real and imaginary parts");
static_assert(std::is_same_v<decltype(std::declval<const complex4f&>().real()), float4>,
    "complex simd member real() should return real_type");
static_assert(std::is_same_v<decltype(std::declval<const complex4f&>().imag()), float4>,
    "complex simd member imag() should return real_type");
static_assert(std::is_same_v<decltype(std::simd::real(std::declval<const complex4f&>())), float4>,
    "free real() should return real_type");
static_assert(std::is_same_v<decltype(std::simd::imag(std::declval<const complex4f&>())), float4>,
    "free imag() should return real_type");
static_assert(noexcept(std::simd::real(std::declval<const complex4f&>())),
    "complex real should be noexcept");
static_assert(noexcept(std::simd::imag(std::declval<const complex4f&>())),
    "complex imag should be noexcept");
static_assert(std::is_same_v<decltype(std::simd::abs(std::declval<const complex4f&>())), float4>,
    "complex abs should return the real_type");
static_assert(std::is_same_v<decltype(std::simd::arg(std::declval<const complex4f&>())), float4>,
    "complex arg should return the real_type");
static_assert(std::is_same_v<decltype(std::simd::norm(std::declval<const complex4f&>())), float4>,
    "complex norm should return the real_type");
static_assert(!noexcept(std::simd::abs(std::declval<const complex4f&>())),
    "complex abs should not be noexcept");
static_assert(!noexcept(std::simd::conj(std::declval<const complex4f&>())),
    "complex conj should not be noexcept");
static_assert(std::is_same_v<decltype(std::simd::conj(std::declval<const complex4f&>())), complex4f>,
    "conj should preserve the complex vector type");
static_assert(std::is_same_v<decltype(std::simd::proj(std::declval<const complex4f&>())), complex4f>,
    "proj should preserve the complex vector type");
static_assert(std::is_same_v<decltype(std::simd::log10(std::declval<const complex4f&>())), complex4f>,
    "complex log10 should preserve the complex vector type");
static_assert(std::is_same_v<decltype(std::simd::pow(std::declval<const complex4f&>(), std::declval<const complex4f&>())), complex4f>,
    "complex pow should preserve the complex vector type");
static_assert(!noexcept(std::simd::pow(std::declval<const complex4f&>(), std::declval<const complex4f&>())),
    "complex pow should not be noexcept");
static_assert(std::is_same_v<decltype(std::simd::polar(std::declval<const float4&>())), complex4f>,
    "polar(real) should return a matching complex vector type");
static_assert(std::is_same_v<decltype(std::simd::polar(std::declval<const float4&>(), std::declval<const float4&>())), complex4f>,
    "polar(real, angle) should return a matching complex vector type");
static_assert(!noexcept(std::simd::polar(std::declval<const float4&>())),
    "polar should not be noexcept");
static_assert(!has_pow<complex4f, complex4d>::value,
    "complex pow should not accept mixed complex simd types");
static_assert(!has_less<complex4f>::value,
    "complex vectors should not expose ordering comparisons");

} // namespace

int main() {
    return 0;
}

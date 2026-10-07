#include <simd>

#include <complex>
#include <concepts>
#include <type_traits>
#include <utility>

namespace {

using int4 = std::simd::vec<int, 4>;
using float4 = std::simd::vec<float, 4>;
using double4 = std::simd::vec<double, 4>;
using complex_float4 = std::simd::vec<std::complex<float>, 4>;
using complex_double4 = std::simd::vec<std::complex<double>, 4>;
using complex_float2 = std::simd::vec<std::complex<float>, 2>;

template<class Target, class Source>
consteval bool nothrow_categories() {
    return std::is_nothrow_constructible_v<Target, Source&> &&
        std::is_nothrow_constructible_v<Target, const Source&> &&
        std::is_nothrow_constructible_v<Target, Source&&> &&
        std::is_nothrow_constructible_v<Target, const Source&&>;
}

template<class Target, class Source>
concept directly_constructible = requires(Source&& source) {
    Target(static_cast<Source&&>(source));
};

static_assert(nothrow_categories<complex_float4, complex_double4>());
static_assert(nothrow_categories<complex_double4, complex_float4>());
static_assert(nothrow_categories<complex_float4, double4>());
static_assert(nothrow_categories<complex_float4, int4>());
static_assert(nothrow_categories<complex_double4, int4>());
static_assert(nothrow_categories<complex_double4, float4>());
static_assert(nothrow_categories<complex_float4, float4>());
static_assert(nothrow_categories<complex_float4, complex_float4>());
static_assert(nothrow_categories<float4, double4>());
static_assert(nothrow_categories<double4, float4>());

using native_complex = std::simd::basic_vec<std::complex<float>>;
using matching_fixed_complex = std::simd::basic_vec<std::complex<float>,
    std::simd::fixed_size_abi<native_complex::size>>;
static_assert(!std::same_as<native_complex, matching_fixed_complex>);
static_assert(nothrow_categories<matching_fixed_complex, native_complex>());
static_assert(nothrow_categories<native_complex, matching_fixed_complex>());

static_assert(directly_constructible<complex_float4, complex_double4>);
static_assert(directly_constructible<complex_float4, double4>);
static_assert(std::is_convertible_v<const int4&, complex_double4>);
static_assert(std::is_convertible_v<const float4&, complex_float4>);
static_assert(!std::is_convertible_v<const double4&, complex_float4>);
static_assert(!std::is_convertible_v<const complex_double4&, complex_float4>);
static_assert(!directly_constructible<complex_float4, complex_float2>);
static_assert(!directly_constructible<complex_float4, const complex_float2&>);
static_assert(!directly_constructible<float4, complex_float4>);
static_assert(!directly_constructible<double4, complex_double4>);
static_assert(directly_constructible<complex_float4, typename int4::mask_type>);

constexpr bool conversions_are_constexpr() {
    const complex_double4 source([](auto i) {
        const double index = static_cast<double>(i);
        return std::complex<double>(index + 0.25, -index - 0.5);
    });
    const complex_float4 narrowed(source);
    const complex_double4 widened(narrowed);
    const double4 reals([](auto i) { return static_cast<double>(i) + 0.75; });
    const complex_float4 from_reals(reals);
    for (std::simd::simd_size_type i = 0; i < complex_float4::size; ++i) {
        if (narrowed[i] != static_cast<std::complex<float>>(source[i]) ||
            widened[i] != static_cast<std::complex<double>>(narrowed[i]) ||
            from_reals[i] != std::complex<float>(static_cast<float>(reals[i]), 0.0f)) {
            return false;
        }
    }
    return true;
}

static_assert(conversions_are_constexpr());

} // namespace

int main() {}

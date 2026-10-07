#include <simd>

#include <gtest/gtest.h>

#include <complex>
#include <type_traits>
#include <utility>

namespace {

using float4 = std::simd::vec<float, 4>;
using double4 = std::simd::vec<double, 4>;
using int4 = std::simd::vec<int, 4>;
using complex_float4 = std::simd::vec<std::complex<float>, 4>;
using complex_double4 = std::simd::vec<std::complex<double>, 4>;

template<class Target, class Source>
void check_categories(const Source& source) {
    static_assert(std::is_nothrow_constructible_v<Target, Source&>);
    static_assert(std::is_nothrow_constructible_v<Target, const Source&>);
    static_assert(std::is_nothrow_constructible_v<Target, Source&&>);
    static_assert(std::is_nothrow_constructible_v<Target, const Source&&>);
    Source mutable_source(source);
    const Target from_lvalue(mutable_source);
    const Target from_const_lvalue(source);
    const Target from_rvalue(std::move(mutable_source));
    const Target from_const_rvalue(std::move(source));
    for (std::simd::simd_size_type i = 0; i < Target::size; ++i) {
        const auto expected = static_cast<typename Target::value_type>(source[i]);
        EXPECT_EQ(from_lvalue[i], expected);
        EXPECT_EQ(from_const_lvalue[i], expected);
        EXPECT_EQ(from_rvalue[i], expected);
        EXPECT_EQ(from_const_rvalue[i], expected);
    }
}

} // namespace

TEST(SimdComplexConversionNoexcept, ComplexConversionsRetainBothComponents) {
    const complex_double4 wide([](auto i) {
        const double index = static_cast<double>(i);
        return std::complex<double>(index + 0.25, -index - 2.5);
    });
    const complex_float4 narrow([](auto i) {
        const float index = static_cast<float>(i);
        return std::complex<float>(index + 1.5f, -index - 3.25f);
    });
    check_categories<complex_float4>(wide);
    check_categories<complex_double4>(narrow);
}

TEST(SimdComplexConversionNoexcept, RealConversionsInitializeImaginaryPartsToZero) {
    const double4 wide([](auto i) { return static_cast<double>(i) + 0.75; });
    const float4 narrow([](auto i) { return static_cast<float>(i) - 1.25f; });
    const int4 integers([](auto i) { return static_cast<int>(i) - 2; });
    check_categories<complex_float4>(wide);
    check_categories<complex_double4>(narrow);
    check_categories<complex_float4>(integers);
    check_categories<complex_double4>(integers);
}

TEST(SimdComplexConversionNoexcept, SameElementConversionsAcrossAbisRemainUsable) {
    using native_complex = std::simd::basic_vec<std::complex<float>>;
    using fixed_complex = std::simd::basic_vec<std::complex<float>,
        std::simd::fixed_size_abi<native_complex::size>>;
    static_assert(!std::is_same_v<native_complex, fixed_complex>);
    const native_complex native([](auto i) {
        const float index = static_cast<float>(i);
        return std::complex<float>(index + 1.0f, index + 10.0f);
    });
    const fixed_complex fixed(native);
    check_categories<fixed_complex>(native);
    check_categories<native_complex>(fixed);
}

TEST(SimdComplexConversionNoexcept, OrdinaryArithmeticConversionsKeepTheirValues) {
    const double4 wide([](auto i) { return static_cast<double>(i) + 0.125; });
    const float4 narrow([](auto i) { return static_cast<float>(i) - 0.5f; });
    check_categories<float4>(wide);
    check_categories<double4>(narrow);
}

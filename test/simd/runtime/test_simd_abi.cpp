#include <simd>

#if defined(__has_include)
#  if __has_include(<stdfloat>)
#    include <stdfloat>
#  endif
#endif

#include <gtest/gtest.h>

#include <type_traits>

namespace {

using int4 = std::simd::vec<int, 4>;
using default_int = std::simd::vec<int>;
using float4 = std::simd::rebind_t<float, int4>;
using int8 = std::simd::resize_t<8, int4>;
using mask4 = std::simd::mask<int, 4>;
using native_int = std::simd::basic_vec<int, std::simd::native_abi<int>>;
using native_uint = std::simd::vec<unsigned int>;
using native_float = std::simd::vec<float>;
using native_double = std::simd::vec<double>;
using deduced4 = std::simd::basic_vec<int, std::simd::deduce_abi_t<int, 4>>;
using disabled_longdouble2 = std::simd::basic_vec<long double, std::simd::fixed_size_abi<2>>;
using disabled_mask3 = std::simd::basic_mask<3, std::simd::fixed_size_abi<4>>;
using disabled_int65 = std::simd::basic_vec<int, std::simd::fixed_size_abi<65>>;

template<class T, class = void>
struct has_size_member : std::false_type {};

template<class T>
struct has_size_member<T, std::void_t<decltype(T::size)>> : std::true_type {};

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

static_assert(std::is_same_v<typename int4::value_type, int>,
    "vec<int, 4> should preserve int as value_type");
static_assert(std::is_same_v<typename float4::value_type, float>,
    "rebind_t should replace the value_type");
static_assert(int4::size == 4, "vec<int, 4> should expose four lanes");
static_assert(mask4::size == 4, "mask<int, 4> should expose four lanes");
static_assert(default_int::size >= 1, "vec<int> should remain a usable default-width alias");
static_assert(int8::size == 8, "resize_t<8, vec<int, 4>> should expose eight lanes");
static_assert(std::simd::alignment_v<int4> >= alignof(int),
    "alignment_v should be at least the scalar alignment");
static_assert(std::simd::alignment_v<int4> >= alignof(int4),
    "alignment_v should remain an ABI-alignment contract that is at least as strong as the object alignment");
static_assert(std::simd::alignment_v<int4, float> == alignof(float) * 4,
    "alignment_v should support vectorizable scalar types beyond the vector value_type");
static_assert(alignof(int4) >= alignof(int),
    "vec<int, 4> object alignment should be at least the scalar alignment");
static_assert(std::is_same_v<decltype(int4::size), const std::integral_constant<std::simd::simd_size_type, 4>>,
    "vec<int, 4>::size should model integral_constant");
static_assert(std::is_same_v<decltype(mask4::size), const std::integral_constant<std::simd::simd_size_type, 4>>,
    "mask<int, 4>::size should model integral_constant");
static_assert(std::is_same_v<typename disabled_longdouble2::value_type, long double>,
    "disabled basic_vec specializations should still expose value_type");
static_assert(std::is_same_v<typename disabled_longdouble2::abi_type, std::simd::fixed_size_abi<2>>,
    "disabled basic_vec specializations should still expose abi_type");
static_assert(std::is_same_v<typename disabled_longdouble2::mask_type, std::simd::basic_mask<sizeof(long double), std::simd::fixed_size_abi<2>>>,
    "disabled basic_vec specializations should still expose mask_type");
static_assert(!has_size_member<disabled_longdouble2>::value,
    "disabled basic_vec specializations should not expose size");
static_assert(!std::is_default_constructible_v<disabled_longdouble2>,
    "disabled basic_vec specializations must delete the default constructor");
static_assert(!std::is_destructible_v<disabled_longdouble2>,
    "disabled basic_vec specializations must delete the destructor");
static_assert(std::is_same_v<typename disabled_mask3::value_type, bool>,
    "disabled basic_mask specializations should still expose value_type");
static_assert(std::is_same_v<typename disabled_mask3::abi_type, std::simd::fixed_size_abi<4>>,
    "disabled basic_mask specializations should still expose abi_type");
static_assert(!has_size_member<disabled_mask3>::value,
    "disabled basic_mask specializations should not expose size");
static_assert(!std::is_default_constructible_v<disabled_mask3>,
    "disabled basic_mask specializations must delete the default constructor");
static_assert(!std::is_default_constructible_v<disabled_int65>,
    "oversized fixed-size ABI specializations should be disabled");

#ifdef __STDCPP_FLOAT16_T__
using stdfloat16_4 = std::simd::vec<std::float16_t, 4>;
static_assert(stdfloat16_4::size == 4, "vec<float16_t, 4> should remain available when std::float16_t is defined");
#endif

#ifdef __STDCPP_FLOAT32_T__
using stdfloat32_4 = std::simd::vec<std::float32_t, 4>;
static_assert(stdfloat32_4::size == 4, "vec<float32_t, 4> should remain available when std::float32_t is defined");
#endif

#ifdef __STDCPP_FLOAT64_T__
using stdfloat64_4 = std::simd::vec<std::float64_t, 4>;
static_assert(stdfloat64_4::size == 4, "vec<float64_t, 4> should remain available when std::float64_t is defined");
#endif

TEST(SimdAbiTest, RebindAndResizePreserveLaneIntent) {
    float4 rebound(1.5f);
    int8 resized(3);

    EXPECT_EQ(float4::size, int4::size);
    EXPECT_EQ(int8::size, 8u);
    EXPECT_FLOAT_EQ(lane(rebound, 0), 1.5f);
    EXPECT_EQ(lane(resized, 0), 3);
    EXPECT_EQ(lane(resized, 7), 3);
}

TEST(SimdAbiTest, DefaultMaskAliasMatchesVectorMaskType) {
    EXPECT_TRUE((std::is_same_v<typename int4::mask_type, mask4>));
}

TEST(SimdAbiTest, DefaultWidthAliasRemainsUsable) {
    default_int values(4);

    EXPECT_GE(default_int::size, 1);
    EXPECT_EQ(lane(values, 0), 4);
}

static_assert(native_int::size >= 1, "native_abi<int> should produce a valid ABI tag");
static_assert(deduced4::size == 4, "deduce_abi_t<int, 4> should preserve the requested lane count");

TEST(SimdAbiExtensionTest, NativeAndDeduceAbiTagsAreUsable) {
    native_int native_value(4);
    deduced4 deduced_value(5);

    EXPECT_GE(native_int::size, 1);
    EXPECT_EQ(deduced4::size, 4);
    EXPECT_EQ(lane(native_value, 0), 4);
    EXPECT_EQ(lane(deduced_value, 3), 5);
}

TEST(SimdAbiTest, NativeAliasesRemainUsableAcrossValueFamilies) {
    native_uint uint_value(5u);
    native_float float_value(1.5f);
    native_double double_value(2.5);

    EXPECT_GE(native_uint::size, 1);
    EXPECT_GE(native_float::size, 1);
    EXPECT_GE(native_double::size, 1);
    EXPECT_EQ(lane(uint_value, 0), 5u);
    EXPECT_FLOAT_EQ(lane(float_value, 0), 1.5f);
    EXPECT_DOUBLE_EQ(lane(double_value, 0), 2.5);
}

} // namespace

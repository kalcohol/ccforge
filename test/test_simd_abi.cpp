#include <simd>

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
using longdouble2 = std::simd::vec<long double, 2>;
using longdouble_mask2 = std::simd::mask<long double, 2>;

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

static_assert(std::is_same<typename int4::value_type, int>::value,
    "vec<int, 4> should preserve int as value_type");
static_assert(std::is_same<typename float4::value_type, float>::value,
    "rebind_t should replace the value_type");
static_assert(int4::size == 4, "vec<int, 4> should expose four lanes");
static_assert(mask4::size == 4, "mask<int, 4> should expose four lanes");
static_assert(default_int::size >= 1, "vec<int> should remain a usable default-width alias");
static_assert(int8::size == 8, "resize_t<8, vec<int, 4>> should expose eight lanes");
static_assert(std::simd::alignment_v<int4> >= alignof(int),
    "alignment_v should be at least the scalar alignment");
static_assert(std::simd::alignment_v<int4> >= alignof(int4),
    "alignment_v should remain an ABI-alignment contract that is at least as strong as the object alignment");
static_assert(alignof(int4) >= alignof(int),
    "vec<int, 4> object alignment should be at least the scalar alignment");
static_assert(std::is_same<decltype(int4::size), const std::integral_constant<std::simd::simd_size_type, 4>>::value,
    "vec<int, 4>::size should model integral_constant");
static_assert(std::is_same<decltype(mask4::size), const std::integral_constant<std::simd::simd_size_type, 4>>::value,
    "mask<int, 4>::size should model integral_constant");

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
    EXPECT_TRUE((std::is_same<typename int4::mask_type, mask4>::value));
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

TEST(SimdAbiTest, LongDoubleFixedSizeAliasesRemainUsable) {
    longdouble2 value(1.0L);
    const longdouble_mask2 selected = value == value;

    EXPECT_EQ(longdouble2::size, 2u);
    EXPECT_TRUE(selected[0]);
    EXPECT_TRUE(selected[1]);
}

} // namespace

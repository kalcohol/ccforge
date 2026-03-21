#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

static_assert(std::is_constructible<int4, int_generator>::value,
    "basic_vec should support generator construction");
static_assert(!std::is_constructible<float4, int_generator>::value,
    "generator construction should reject non-value-preserving lane conversions");
static_assert(!std::is_constructible<float4, int4>::value,
    "flag_convert-free conversion should be rejected when not supported");
static_assert(std::is_constructible<longlong4, int4>::value,
    "value-preserving vector widening should be constructible without std::simd::flag_convert");
static_assert(std::is_constructible<float4, int4, std::simd::flags<std::simd::convert_flag>>::value,
    "flag_convert overload should always be available");
static_assert(!std::is_convertible<int, float4>::value,
    "scalar broadcasting should reject non-value-preserving implicit conversions");
static_assert(std::is_constructible<int4, std::integral_constant<int, 1>>::value,
    "vec<int,4> must be constructible from integral_constant<int,1>");
static_assert(std::is_constructible<longlong4, std::integral_constant<int, 1>>::value,
    "vec<long long,4> must be constructible from integral_constant<int,1> when value-preserving");
static_assert(std::is_constructible<int4, bool>::value,
    "vec<int,4> must accept bool scalar broadcasts");
static_assert(std::is_constructible<int4, explicit_to_int>::value,
    "vec<int,4> must accept explicitly-convertible scalar broadcasts");
static_assert(!std::is_convertible<explicit_to_int, int4>::value,
    "explicit scalar broadcasts should remain explicit when the source is not implicitly convertible to the lane type");
static_assert(std::is_constructible<int4, wrapper_bad_value>::value,
    "non-wrapper implicit conversions should still construct when they are explicitly convertible to the lane type");
static_assert(std::is_convertible<wrapper_bad_value, int4>::value,
    "types that fail constexpr-wrapper-like should still use the ordinary implicit scalar-broadcast path when convertible");
static_assert(std::is_constructible<int4, std::span<const int, 4>>::value,
    "basic_vec should support contiguous fixed-extent range construction");
static_assert(std::is_constructible<int4, std::span<const int, 4>, mask4>::value,
    "vec<int,4> must be constructible from span<const int,4> and mask");
static_assert(!std::is_constructible<int4, std::span<const int>>::value,
    "basic_vec must reject dynamic-extent contiguous ranges");
static_assert(!std::is_constructible<int4, std::span<const int>, mask4>::value,
    "masked basic_vec range construction must reject dynamic-extent contiguous ranges");
static_assert(std::is_constructible<mask4, bool>::value,
    "basic_mask should support scalar bool broadcast construction");
static_assert(std::is_constructible<mask4, unsigned int>::value,
    "basic_mask should support unsigned integral bit-pattern construction");
static_assert(std::is_constructible<mask4, std::bitset<4>>::value,
    "basic_mask should support std::bitset construction");
static_assert(std::is_convertible<std::bitset<4>, mask4>::value,
    "basic_mask bitset construction should be implicit");
static_assert(std::is_constructible<mask4, const byte_mask4&>::value,
    "basic_mask should support same-width cross-mask construction");
static_assert(!std::is_convertible<byte_mask4, mask4>::value,
    "basic_mask cross-mask construction should remain explicit");
static_assert(!std::is_convertible<unsigned int, mask4>::value,
    "basic_mask unsigned bit-pattern construction should remain explicit");
static_assert(std::is_constructible<mask4, bool_generator>::value,
    "basic_mask should support generator construction");
static_assert(!std::is_constructible<mask4, int>::value,
    "mask<int,4> must not be constructible from int");
static_assert(!std::is_constructible<mask4, int_returning_generator>::value,
    "basic_mask must not accept generator returning int");
static_assert(!std::is_constructible<mask4, std::span<const int, 4>>::value,
    "basic_mask must not expose a public contiguous-range constructor");

} // namespace

int main() {
    simd_test::int4 values(simd_test::explicit_to_int{});
    simd_test::int4 from_bool(true);
    simd_test::mask4 selected(true);
    return values[0] == 7 && from_bool[0] == 1 && selected[0] ? 0 : 1;
}

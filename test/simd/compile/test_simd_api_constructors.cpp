#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

static_assert(std::is_constructible<int4, int_generator>::value,
    "basic_vec should support generator construction");
static_assert(!std::is_constructible<float4, int_generator>::value,
    "generator construction should reject non-value-preserving lane conversions");
static_assert(std::is_constructible<float4, int4>::value,
    "vector converting constructors should support explicit lane conversions");
static_assert(!std::is_convertible<int4, float4>::value,
    "non-value-preserving vector conversions should remain explicit");
static_assert(std::is_constructible<longlong4, int4>::value,
    "value-preserving vector widening should be constructible without std::simd::flag_convert");
static_assert(std::is_convertible<int4, longlong4>::value,
    "value-preserving vector widening with increasing rank should be implicit");
static_assert(std::is_convertible<long4, longlong4>::value,
    "an integral conversion with increasing rank should remain implicit even when representations match");
static_assert(!std::is_convertible<longlong4, long4>::value,
    "an integral conversion with decreasing rank should be explicit");
static_assert(!std::is_constructible<int4, int3>::value,
    "vector converting constructors should reject mismatched lane counts in their constraints");
static_assert(std::is_constructible<complex4d, complex4f>::value,
    "complex vector lane conversions should be explicitly constructible");
static_assert(!std::is_convertible<complex4f, complex4d>::value,
    "complex vector lane conversions are not arithmetic value-preserving conversions");
static_assert(std::is_constructible<float4, int4, std::simd::flags<std::simd::convert_flag>>::value,
    "flag_convert overload should always be available");
static_assert(!std::is_constructible<int4, double>::value,
    "scalar broadcasting should reject non-value-preserving arithmetic conversions even when direct-initialized");
static_assert(!std::is_convertible<int, float4>::value,
    "scalar broadcasting should reject non-value-preserving implicit conversions");
static_assert(std::is_constructible<int4, std::integral_constant<int, 1>>::value,
    "vec<int,4> must be constructible from integral_constant<int,1>");
static_assert(std::is_constructible<longlong4, std::integral_constant<int, 1>>::value,
    "vec<long long,4> must be constructible from integral_constant<int,1> when value-preserving");
static_assert(std::is_constructible<int4, bool>::value,
    "vec<int,4> must accept bool scalar broadcasts");
static_assert(!std::is_constructible<int4, explicit_to_int>::value,
    "non-arithmetic scalar broadcasts must satisfy convertible_to rather than explicit convertibility alone");
static_assert(std::is_constructible<int4, wrapper_bad_value>::value,
    "non-wrapper implicit conversions should still construct when convertible to the lane type");
static_assert(std::is_convertible<wrapper_bad_value, int4>::value,
    "types that fail constexpr-wrapper-like should still use the ordinary implicit scalar-broadcast path when convertible");
static_assert(!std::is_constructible<uint4, std::integral_constant<int, -1>>::value,
    "constexpr wrapper broadcasts should reject values outside the target integer range");
static_assert(!std::is_constructible<float4, std::integral_constant<int, 16'777'217>>::value,
    "constexpr wrapper broadcasts should reject integer values that are not exactly representable in the target float");
static_assert(std::is_constructible<int4, std::integral_constant<double, 1.0>>::value,
    "constexpr wrapper broadcasts should accept exactly representable floating values");
static_assert(!std::is_constructible<int4, std::integral_constant<double, 1.5>>::value,
    "constexpr wrapper broadcasts should reject non-integral floating values");
static_assert(!std::is_constructible<int4, std::integral_constant<double, 1.0e100>>::value,
    "out-of-range constexpr wrapper probes should fail by constraints rather than hard errors");
static_assert(std::is_constructible<int4, index_object_generator>::value,
    "generator results that are non-arithmetic but convertible to the lane type should be accepted");
static_assert(std::is_constructible<int4, std::span<const int, 4>>::value,
    "basic_vec should support contiguous fixed-extent range construction");
static_assert(std::is_convertible<std::span<const int, 4>, int4>::value,
    "basic_vec fixed-extent range construction should be implicit");
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
static_assert(!std::is_constructible<int4, std::array<std::complex<float>, 4>>::value,
    "basic_vec range construction must reject lane values that are not explicitly convertible");

} // namespace

int main() {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::simd::basic_vec deduced_from_range{input};
    simd_test::int4 values(simd_test::wrapper_bad_value{});
    simd_test::int4 from_bool(true);
    simd_test::mask4 selected(true);
    std::simd::basic_vec deduced_from_mask{selected};

    static_assert(std::is_same_v<decltype(deduced_from_range), simd_test::int4>);
    static_assert(std::is_same_v<decltype(deduced_from_mask), simd_test::int4>);

    return values[0] == 6 && from_bool[0] == 1 && selected[0] &&
            deduced_from_range[3] == 4 && deduced_from_mask[0] == 1
        ? 0
        : 1;
}

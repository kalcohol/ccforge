#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

constexpr bool constexpr_broadcast_reduce() {
    constexpr int4 values(42);
    return values[0] == 42 && values[3] == 42 && std::simd::reduce(values) == 168;
}

constexpr bool constexpr_generator_and_iota() {
    constexpr int4 generated(int_generator{});
    constexpr auto sequence = std::simd::iota<int4>(3);
    return generated[0] == 1 && generated[3] == 7 && sequence[0] == 3 && sequence[3] == 6;
}

consteval bool consteval_flag_union() {
    using combined_type = decltype(std::simd::flag_default | std::simd::flag_convert);
    return std::is_same<combined_type, std::simd::flags<std::simd::convert_flag>>::value;
}

template<class V, class Shift, class = void>
struct has_vector_lshift_assign : std::false_type {};

template<class V, class Shift>
struct has_vector_lshift_assign<V,
                                Shift,
                                std::void_t<decltype(std::declval<V&>() <<= std::declval<const Shift&>())>>
    : std::true_type {};

template<class V, class Shift, class = void>
struct has_vector_rshift_assign : std::false_type {};

template<class V, class Shift>
struct has_vector_rshift_assign<V,
                                Shift,
                                std::void_t<decltype(std::declval<V&>() >>= std::declval<const Shift&>())>>
    : std::true_type {};

template<class V, class Shift, class = void>
struct has_vector_lshift : std::false_type {};

template<class V, class Shift>
struct has_vector_lshift<V,
                         Shift,
                         std::void_t<decltype(std::declval<V>() << std::declval<const Shift&>())>>
    : std::true_type {};

template<class V, class Shift, class = void>
struct has_vector_rshift : std::false_type {};

template<class V, class Shift>
struct has_vector_rshift<V,
                         Shift,
                         std::void_t<decltype(std::declval<V>() >> std::declval<const Shift&>())>>
    : std::true_type {};

static_assert(std::is_same<typename int4::value_type, int>::value,
    "vec<int, 4> should expose int as value_type");
static_assert(std::is_signed<std::simd::simd_size_type>::value,
    "simd_size_type should be a signed integer type");
static_assert(std::is_same<typename float4::value_type, float>::value,
    "rebind_t should switch the value_type");
static_assert(int4::size == 4, "vec<int, 4> should expose four lanes");
static_assert(mask4::size == 4, "mask<int, 4> should expose four lanes");
static_assert(default_int::size >= 1,
    "vec<int> should remain a usable default-width alias");
static_assert(int8::size == 8, "resize_t should expose the requested lane count");
static_assert(std::simd::alignment_v<int4> >= alignof(int),
    "alignment_v should report at least the scalar alignment");
static_assert(std::simd::alignment_v<int4> >= alignof(int4),
    "alignment_v should remain an ABI-alignment contract that is at least as strong as the object alignment");
static_assert(alignof(int4) >= alignof(int),
    "basic_vec object alignment should remain at least the scalar alignment");
static_assert(std::is_same<typename int4::mask_type, mask4>::value,
    "vec<int, 4>::mask_type should match mask<int, 4>");
static_assert(constexpr_broadcast_reduce(),
    "basic_vec broadcast construction and reduce should be constexpr-capable");
static_assert(constexpr_generator_and_iota(),
    "generator construction and iota should be constexpr-capable");
static_assert(consteval_flag_union(),
    "std::simd::flags operator| should remain usable in immediate contexts");
static_assert(std::is_same<decltype(std::declval<const int4&>()[std::simd::simd_size_type{}]), int>::value,
    "const operator[] should return the lane value by value");
static_assert(std::is_same<decltype(std::declval<int4&>()[std::simd::simd_size_type{}]), int>::value,
    "non-const operator[] should also return the lane value by value");
static_assert(!std::is_assignable<decltype((std::declval<int4&>()[std::simd::simd_size_type{}])), int>::value,
    "operator[] should not expose writable lane references");
static_assert(std::is_same<decltype(std::declval<int4&>() <<= std::declval<const int4&>()), int4&>::value,
    "integral vec should expose per-lane left-shift assignment");
static_assert(std::is_same<decltype(std::declval<int4&>() >>= std::declval<const int4&>()), int4&>::value,
    "integral vec should expose per-lane right-shift assignment");
static_assert(std::is_same<decltype(std::declval<int4>() << std::declval<const int4&>()), int4>::value,
    "integral vec should expose per-lane left shift");
static_assert(std::is_same<decltype(std::declval<int4>() >> std::declval<const int4&>()), int4>::value,
    "integral vec should expose per-lane right shift");
static_assert(!has_vector_lshift_assign<float4, float4>::value,
    "floating vec should not expose per-lane left-shift assignment");
static_assert(!has_vector_rshift_assign<float4, float4>::value,
    "floating vec should not expose per-lane right-shift assignment");
static_assert(!has_vector_lshift<float4, float4>::value,
    "floating vec should not expose per-lane left shift");
static_assert(!has_vector_rshift<float4, float4>::value,
    "floating vec should not expose per-lane right shift");

} // namespace

int main() {
    int input[4] = {1, 2, 3, 4};
    int output[4] = {};

    simd_test::default_int default_values(5);
    simd_test::int4 generated(simd_test::int_generator{});
    simd_test::mask4 selected(simd_test::bool_generator{});
    const auto partial = std::simd::partial_load<simd_test::int4>(input, 3, selected);
    std::simd::partial_store(partial, output, 3, selected);

    auto begin = generated.begin();
    auto end = generated.end();
    const simd_test::int4& const_generated = generated;
    auto cbegin = const_generated.cbegin();
    auto cend = const_generated.cend();

    return simd_test::lane(default_values, 0) == 5 &&
        simd_test::lane(generated, 0) == 1 &&
        simd_test::lane(generated, 3) == 7 &&
        simd_test::lane(selected, 0) && !simd_test::lane(selected, 1) &&
        output[0] == 1 &&
        output[1] == 0 &&
        output[2] == 3 &&
        output[3] == 0 &&
        (end - begin) == 4 &&
        (cend - cbegin) == 4 ? 0 : 1;
}

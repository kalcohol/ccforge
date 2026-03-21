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
static_assert(std::is_same<decltype(std::declval<int4&>().begin()), typename int4::iterator>::value,
    "begin() should return iterator");
static_assert(std::is_same<decltype(std::declval<const int4&>()[std::simd::simd_size_type{}]), int>::value,
    "const operator[] should return the lane value by value");
static_assert(std::is_same<decltype(std::declval<int4&>()[std::simd::simd_size_type{}]), int>::value,
    "non-const operator[] should also return the lane value by value");
static_assert(!std::is_assignable<decltype((std::declval<int4&>()[std::simd::simd_size_type{}])), int>::value,
    "operator[] should not expose writable lane references");
static_assert(std::is_same<decltype(std::declval<const mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "const mask operator[] should return bool by value");
static_assert(std::is_same<decltype(std::declval<mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "non-const mask operator[] should also return bool by value");
static_assert(!std::is_assignable<decltype((std::declval<mask4&>()[std::simd::simd_size_type{}])), bool>::value,
    "mask operator[] should not expose writable lane references");
static_assert(std::is_same<decltype(std::declval<mask4&>() & std::declval<const mask4&>()), mask4>::value,
    "mask bitwise and should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() | std::declval<const mask4&>()), mask4>::value,
    "mask bitwise or should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() ^ std::declval<const mask4&>()), mask4>::value,
    "mask bitwise xor should remain on the mask type");
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
static_assert(std::is_same<decltype(std::declval<const mask4&>().to_bitset()), std::bitset<4>>::value,
    "basic_mask::to_bitset should expose a matching std::bitset");
static_assert(std::is_same<decltype(std::declval<const mask4&>().to_ullong()), unsigned long long>::value,
    "basic_mask::to_ullong should expose an unsigned long long bit pattern");
static_assert(std::is_same<decltype(static_cast<int4>(std::declval<const mask4&>())), int4>::value,
    "basic_mask should provide same-width vector conversion");
static_assert(std::is_convertible<mask4, int4>::value,
    "basic_mask to same-byte basic_vec conversion should be implicit");
static_assert(std::is_constructible<longlong4, mask4>::value,
    "basic_mask should still support explicit different-byte vector conversion");
static_assert(!std::is_convertible<mask4, longlong4>::value,
    "basic_mask to different-byte basic_vec conversion should remain explicit");
static_assert(std::is_same<decltype(+std::declval<const mask4&>()), int4>::value,
    "mask unary plus should expose the signed integer lane vector");
static_assert(std::is_same<decltype(-std::declval<const mask4&>()), int4>::value,
    "mask unary minus should expose the signed integer lane vector");
static_assert(std::is_same<decltype(~std::declval<const mask4&>()), int4>::value,
    "mask unary bitwise not should expose the signed integer lane vector");
static_assert(std::is_same<decltype(std::declval<const mask4&>() == std::declval<const mask4&>()), mask4>::value,
    "mask equality should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() != std::declval<const mask4&>()), mask4>::value,
    "mask inequality should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() < std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() <= std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() > std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() >= std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<typename int4::iterator&>()++), typename int4::iterator>::value,
    "iterator post-increment should return iterator");
static_assert(std::is_same<decltype(std::declval<typename int4::iterator&>()--), typename int4::iterator>::value,
    "iterator post-decrement should return iterator");
static_assert(std::is_same<typename int4::iterator::reference, int>::value,
    "iterator::reference should be the scalar value type");
static_assert(std::is_same<typename int4::const_iterator::reference, int>::value,
    "const_iterator::reference should be the scalar value type");
static_assert(std::is_same<typename int4::iterator::iterator_category, std::input_iterator_tag>::value,
    "iterator_category should remain input_iterator_tag for legacy algorithms");
static_assert(std::is_same<typename int4::iterator::iterator_concept, std::random_access_iterator_tag>::value,
    "iterator_concept should model random access traversal");
static_assert(std::is_same<decltype(std::declval<const int4&>().begin()), typename int4::const_iterator>::value,
    "const begin() should return const_iterator");
static_assert(std::is_same<decltype(std::declval<const int4&>().cbegin()), typename int4::const_iterator>::value,
    "cbegin() should return const_iterator");
static_assert(std::is_same<decltype(std::declval<int4&>().end()), std::default_sentinel_t>::value,
    "end() should return default_sentinel_t");
static_assert(std::is_same<decltype(std::declval<const int4&>().cend()), std::default_sentinel_t>::value,
    "cend() should return default_sentinel_t");
static_assert(std::is_same<decltype(std::declval<int4&>().end() - std::declval<int4&>().begin()), std::simd::simd_size_type>::value,
    "default sentinel difference should use simd_size_type");
static_assert(std::is_same<decltype(std::declval<int4&>().begin() - std::declval<int4&>().end()), std::simd::simd_size_type>::value,
    "iterator minus default sentinel should use simd_size_type");

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

#include <simd>

#include <array>
#include <bitset>
#include <tuple>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using int4 = std::simd::vec<int, 4>;
using int3 = std::simd::vec<int, 3>;
using int2 = std::simd::vec<int, 2>;
using int1 = std::simd::vec<int, 1>;
using default_int = std::simd::vec<int>;
using float4 = std::simd::rebind_t<float, int4>;
using int8 = std::simd::resize_t<8, int4>;
using longlong4 = std::simd::vec<long long, 4>;
using mask4 = std::simd::mask<int, 4>;
using byte_mask4 = std::simd::mask<signed char, 4>;
using int_iter = std::vector<int>::iterator;
using const_int_iter = std::vector<int>::const_iterator;

struct fake_index_vector {
    using value_type = int;
    static constexpr std::simd::simd_size_type size = 4;

    int operator[](std::simd::simd_size_type) const noexcept;
};

template<class V, class Indices, class = void>
struct has_dynamic_permute : std::false_type {};

template<class V, class Indices>
struct has_dynamic_permute<V, Indices, std::void_t<decltype(std::simd::permute(std::declval<const V&>(), std::declval<const Indices&>()))>> : std::true_type {};

template<class V, class Indices, class = void>
struct has_dynamic_subscript : std::false_type {};

template<class V, class Indices>
struct has_dynamic_subscript<V, Indices, std::void_t<decltype(std::declval<const V&>()[std::declval<const Indices&>()])>> : std::true_type {};

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

struct int_generator {
    template<class Index>
    constexpr int operator()(Index) const noexcept {
        return static_cast<int>(Index::value * 2 + 1);
    }
};

struct bool_generator {
    template<class Index>
    constexpr bool operator()(Index) const noexcept {
        return (Index::value % 2) == 0;
    }
};

#if defined(FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)

struct identity_index_map {
    template<class Index>
    constexpr std::simd::simd_size_type operator()(Index) const noexcept {
        return Index::value;
    }
};

struct reverse_with_size_map {
    template<class Index, class Size>
    constexpr std::simd::simd_size_type operator()(Index, Size) const noexcept {
        return static_cast<std::simd::simd_size_type>(Size::value - 1 - Index::value);
    }
};

using chunk_input = std::simd::resize_t<8, int4>;
using chunk_by_type_result = decltype(std::simd::chunk<int4>(std::declval<const chunk_input&>()));
using chunk_by_width_result = decltype(std::simd::chunk<2>(std::declval<const chunk_input&>()));
using uneven_chunk_result = decltype(std::simd::chunk<3>(std::declval<const int4&>()));
using cat_result = decltype(std::simd::cat(std::declval<const int4&>(), std::declval<const int4&>()));
using static_permute_result = decltype(std::simd::permute(std::declval<const int4&>(), identity_index_map{}));
using sized_static_permute_result = decltype(std::simd::permute(std::declval<const int4&>(), reverse_with_size_map{}));
using dynamic_permute_result = decltype(std::simd::permute(std::declval<const int4&>(), std::declval<const int4&>()));
using indexed_subscript_result = decltype(std::declval<const int4&>()[std::declval<const int4&>()]);

#endif

#if defined(FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)
static_assert(std::is_same<chunk_by_type_result, std::array<int4, 2>>::value,
    "chunk<int4>(resize_t<8, int4>) should produce two int4 chunks");
static_assert(std::is_same<chunk_by_width_result, std::array<int2, 4>>::value,
    "chunk<2>(resize_t<8, int4>) should produce four int2 chunks");
static_assert(std::tuple_size<uneven_chunk_result>::value == 2,
    "chunk<3>(int4) should return array-plus-tail for uneven widths");
static_assert(std::is_same<typename std::tuple_element<0, uneven_chunk_result>::type, std::array<int3, 1>>::value,
    "chunk<3>(int4) should return one int3 full chunk");
static_assert(std::is_same<typename std::tuple_element<1, uneven_chunk_result>::type, int1>::value,
    "chunk<3>(int4) should return an int1 tail chunk");
static_assert(std::is_same<cat_result, int8>::value,
    "cat(int4, int4) should produce resize_t<8, int4>");
static_assert(std::is_same<static_permute_result, int4>::value,
    "permute(value, index_map) should preserve lane count by default");
static_assert(std::is_same<sized_static_permute_result, int4>::value,
    "permute(value, index_map_with_size) should preserve lane count by default");
static_assert(std::is_same<dynamic_permute_result, int4>::value,
    "permute(value, indices) should preserve the index vector lane count");
static_assert(std::is_same<indexed_subscript_result, int4>::value,
    "value[index_vector] should return the permuted vector type");
static_assert(has_dynamic_permute<int4, int4>::value,
    "simd<int> should remain a valid dynamic permute index vector");
static_assert(!has_dynamic_permute<int4, fake_index_vector>::value,
    "lookalike integral types should not satisfy the dynamic permute index-vector constraint");
static_assert(!has_dynamic_subscript<int4, fake_index_vector>::value,
    "operator[](indices) should reject non-data-parallel lookalike index vectors");
#endif

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
static_assert(std::is_same<typename int4::mask_type, mask4>::value,
    "vec<int, 4>::mask_type should match mask<int, 4>");
static_assert(std::is_constructible<int4, int_generator>::value,
    "basic_vec should support generator construction");
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
static_assert(std::is_same<decltype(std::declval<int4&>().begin()), typename int4::iterator>::value,
    "begin() should return iterator");
static_assert(std::is_same<decltype(std::declval<const int4&>()[std::simd::simd_size_type{}]), int>::value,
    "const operator[] should return the lane value by value");
static_assert(std::is_same<decltype(std::declval<int4&>()[std::simd::simd_size_type{}]), int>::value,
    "non-const operator[] should also return the lane value by value");
static_assert(std::is_same<decltype(std::declval<const mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "const mask operator[] should return bool by value");
static_assert(std::is_same<decltype(std::declval<mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "non-const mask operator[] should also return bool by value");
static_assert(std::is_same<decltype(std::declval<mask4&>() & std::declval<const mask4&>()), mask4>::value,
    "mask bitwise and should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() | std::declval<const mask4&>()), mask4>::value,
    "mask bitwise or should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() ^ std::declval<const mask4&>()), mask4>::value,
    "mask bitwise xor should remain on the mask type");
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
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{})), int4>::value,
    "partial_load(pointer, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<longlong4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{})), longlong4>::value,
    "value-preserving partial_load should not require std::simd::flag_convert");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{})), int4>::value,
    "partial_load(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "partial_load(pointer, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "partial_load(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr))), int4>::value,
    "partial_load(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>())), int4>::value,
    "partial_load(iterator, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr), mask4{})), int4>::value,
    "partial_load(pointer, sentinel, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>(), mask4{})), int4>::value,
    "partial_load(iterator, sentinel, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{})), void>::value,
    "partial_store(pointer, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), static_cast<long long*>(nullptr), std::simd::simd_size_type{})), void>::value,
    "value-preserving partial_store should not require std::simd::flag_convert");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), std::declval<int_iter>(), std::simd::simd_size_type{})), void>::value,
    "partial_store(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{}, mask4{})), void>::value,
    "partial_store(pointer, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), std::declval<int_iter>(), std::simd::simd_size_type{}, mask4{})), void>::value,
    "partial_store(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), static_cast<int*>(nullptr), static_cast<int*>(nullptr))), void>::value,
    "partial_store(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), std::declval<int_iter>(), std::declval<int_iter>())), void>::value,
    "partial_store(iterator, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), static_cast<int*>(nullptr), static_cast<int*>(nullptr), mask4{})), void>::value,
    "partial_store(pointer, sentinel, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), std::declval<int_iter>(), std::declval<int_iter>(), mask4{})), void>::value,
    "partial_store(iterator, sentinel, mask) should be a public entry point");

static_assert(std::is_same<decltype(std::simd::select(mask4{}, mask4{}, mask4{})), mask4>::value,
    "select(mask, mask, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::simd_cast<longlong4>(std::declval<const int4&>())), longlong4>::value,
    "simd_cast should be a public entry point for value-preserving conversions");
static_assert(std::is_same<decltype(std::simd::static_simd_cast<int4>(std::declval<const longlong4&>())), int4>::value,
    "static_simd_cast should be a public entry point for narrowing conversions");

static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) = value; },
    "where(mask, vec) should allow masked assignment from vectors");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) = 1; },
    "where(mask, vec) should allow masked assignment from scalars");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value).copy_from(static_cast<const int*>(nullptr)); },
    "where(mask, vec) should expose copy_from for pointers");
static_assert(requires(const int4 value, mask4 mask_value) { std::simd::where(mask_value, value).copy_to(static_cast<int*>(nullptr)); },
    "where(mask, vec) should expose copy_to for const vectors");

static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, int4{})), int4>::value,
    "partial_gather_from(pointer, count, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{})), int4>::value,
    "partial_gather_from(pointer, count, indices, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const int*>(nullptr), int4{})), int4>::value,
    "unchecked_gather_from(pointer, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const int*>(nullptr), int4{}, mask4{})), int4>::value,
    "unchecked_gather_from(pointer, indices, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{}, int4{})), void>::value,
    "partial_scatter_to(value, pointer, count, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{})), void>::value,
    "partial_scatter_to(value, pointer, count, indices, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), int4{})), void>::value,
    "unchecked_scatter_to(value, pointer, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), int4{}, mask4{})), void>::value,
    "unchecked_scatter_to(value, pointer, indices, mask) should be a public entry point");

static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const float*>(nullptr), std::simd::simd_size_type{}, int4{}, std::simd::flag_convert)), int4>::value,
    "partial_gather_from(flag_convert) should accept type-changing loads");
static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const float*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{}, std::simd::flag_convert)), int4>::value,
    "partial_gather_from(flag_convert) should accept type-changing loads with masks");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const float*>(nullptr), int4{}, std::simd::flag_convert)), int4>::value,
    "unchecked_gather_from(flag_convert) should accept type-changing loads");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const float*>(nullptr), int4{}, mask4{}, std::simd::flag_convert)), int4>::value,
    "unchecked_gather_from(flag_convert) should accept type-changing loads with masks");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<float*>(nullptr), std::simd::simd_size_type{}, int4{}, std::simd::flag_convert)), void>::value,
    "partial_scatter_to(flag_convert) should accept type-changing stores");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<float*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{}, std::simd::flag_convert)), void>::value,
    "partial_scatter_to(flag_convert) should accept type-changing stores with masks");
static_assert(std::is_same<decltype(std::simd::unchecked_scatter_to(std::declval<const int4&>(), static_cast<float*>(nullptr), int4{}, std::simd::flag_convert)), void>::value,
    "unchecked_scatter_to(flag_convert) should accept type-changing stores");
static_assert(std::is_same<decltype(std::simd::unchecked_scatter_to(std::declval<const int4&>(), static_cast<float*>(nullptr), int4{}, mask4{}, std::simd::flag_convert)), void>::value,
    "unchecked_scatter_to(flag_convert) should accept type-changing stores with masks");

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES)

static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr))), int4>::value,
    "unchecked_load(pointer) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{})), int4>::value,
    "unchecked_load(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr), mask4{})), int4>::value,
    "unchecked_load(pointer, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "unchecked_load(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr), std::simd::flag_default)), int4>::value,
    "unchecked_load(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>(), std::simd::flag_default)), int4>::value,
    "unchecked_load(iterator, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), static_cast<int*>(nullptr))), void>::value,
    "unchecked_store(pointer) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), std::declval<int_iter>(), std::simd::simd_size_type{})), void>::value,
    "unchecked_store(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), static_cast<int*>(nullptr), mask4{})), void>::value,
    "unchecked_store(pointer, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), std::declval<int_iter>(), std::simd::simd_size_type{}, mask4{})), void>::value,
    "unchecked_store(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), static_cast<int*>(nullptr), static_cast<int*>(nullptr), std::simd::flag_default)), void>::value,
    "unchecked_store(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), std::declval<int_iter>(), std::declval<int_iter>(), std::simd::flag_default)), void>::value,
    "unchecked_store(iterator, sentinel) should be a public entry point");

#endif

} // namespace

int main() {
    int input[4] = {1, 2, 3, 4};
    int output[4] = {};

    default_int default_values(5);
    int4 generated(int_generator{});
    mask4 selected(bool_generator{});
    const auto partial = std::simd::partial_load<int4>(input, 3, selected);
    std::simd::partial_store(partial, output, 3, selected);

    auto begin = generated.begin();
    auto end = generated.end();
    const int4& const_generated = generated;
    auto cbegin = const_generated.cbegin();
    auto cend = const_generated.cend();

    return lane(default_values, 0) == 5 &&
        lane(generated, 0) == 1 &&
        lane(generated, 3) == 7 &&
        lane(selected, 0) && !lane(selected, 1) &&
        output[0] == 1 &&
        output[1] == 0 &&
        output[2] == 3 &&
        output[3] == 0 &&
        (end - begin) == 4 &&
        (cend - cbegin) == 4 ? 0 : 1;
}

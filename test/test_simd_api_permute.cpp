#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

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

} // namespace

int main() {
    return 0;
}

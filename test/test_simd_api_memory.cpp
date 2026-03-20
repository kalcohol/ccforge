#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

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
static_assert(has_partial_load_range<int4, std::span<const int, 4>>::value,
    "partial_load(range) should be a public entry point for contiguous ranges");
static_assert(has_partial_store_range<int4, std::span<int, 4>>::value,
    "partial_store(range) should be a public entry point for contiguous ranges");
static_assert(!has_partial_load_count<int4, deque_int_iter>::value,
    "partial_load(iterator, count) should reject non-contiguous iterators");
static_assert(!has_partial_store_count<int4, deque_int_iter>::value,
    "partial_store(iterator, count) should reject non-contiguous iterators");
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
static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, int4{})), int4>::value,
    "partial_gather_from(pointer, count, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_gather_from<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{})), int4>::value,
    "partial_gather_from(pointer, count, indices, mask) should be a public entry point");
static_assert(has_partial_gather_range<int4, std::span<const int, 8>, int4>::value,
    "partial_gather_from(range, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const int*>(nullptr), int4{})), int4>::value,
    "unchecked_gather_from(pointer, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_gather_from<int4>(static_cast<const int*>(nullptr), int4{}, mask4{})), int4>::value,
    "unchecked_gather_from(pointer, indices, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{}, int4{})), void>::value,
    "partial_scatter_to(value, pointer, count, indices) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(), static_cast<int*>(nullptr), std::simd::simd_size_type{}, int4{}, mask4{})), void>::value,
    "partial_scatter_to(value, pointer, count, indices, mask) should be a public entry point");
static_assert(has_partial_scatter_range<int4, std::span<int, 8>, int4>::value,
    "partial_scatter_to(value, range, indices) should be a public entry point");
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
static_assert(std::is_same<
    decltype(std::simd::partial_gather_from<longlong4>(
        static_cast<const long long*>(nullptr),
        std::simd::simd_size_type{},
        int4{},
        mask4{},
        std::simd::flag_default)),
    longlong4>::value,
    "masked gather overloads should use Indices::mask_type");

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
    return 0;
}

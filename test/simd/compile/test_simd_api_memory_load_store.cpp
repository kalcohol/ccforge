#include "simd_test_common.hpp"

#include <complex>
#include <span>

namespace {

using namespace simd_test;

static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{})), int4>::value,
    "partial_load(pointer, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load(static_cast<const int*>(nullptr), std::simd::simd_size_type{})), std::simd::basic_vec<int>>::value,
    "partial_load(pointer, count) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::partial_load<longlong4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{})), longlong4>::value,
    "value-preserving partial_load should not require std::simd::flag_convert");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{})), int4>::value,
    "partial_load(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load(std::declval<const_int_iter>(), std::simd::simd_size_type{})), std::simd::basic_vec<int>>::value,
    "partial_load(iterator, count) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "partial_load(pointer, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "partial_load(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr))), int4>::value,
    "partial_load(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr))), std::simd::basic_vec<int>>::value,
    "partial_load(pointer, sentinel) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>())), int4>::value,
    "partial_load(iterator, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr), mask4{})), int4>::value,
    "partial_load(pointer, sentinel, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::partial_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>(), mask4{})), int4>::value,
    "partial_load(iterator, sentinel, mask) should be a public entry point");
static_assert(has_partial_load_range<int4, std::span<const int, 4>>::value,
    "partial_load(range) should be a public entry point for contiguous ranges");
static_assert(std::is_same<decltype(std::simd::partial_load(std::declval<std::span<const int, 4>>())), std::simd::basic_vec<int>>::value,
    "partial_load(range) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::partial_load(std::declval<std::span<const int, 4>>(),
                                                            typename std::simd::basic_vec<int>::mask_type{})),
                           std::simd::basic_vec<int>>::value,
    "partial_load(range, mask) should provide the standard default vector type");
static_assert(has_partial_store_range<int4, std::span<int, 4>>::value,
    "partial_store(range) should be a public entry point for contiguous ranges");
static_assert(std::is_same<decltype(std::simd::partial_store(std::declval<const int4&>(), std::declval<std::span<int, 4>>(), mask4{})), void>::value,
    "partial_store(value, range, mask) should be a public entry point");
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

static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const std::complex<float>*>(nullptr), std::simd::simd_size_type{})), default_complexf>,
    "partial_load(pointer, count) should infer supported complex value types");
static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const std::complex<float>*>(nullptr),
                                                              std::simd::simd_size_type{},
                                                              typename default_complexf::mask_type{})),
                             default_complexf>,
    "masked partial_load(pointer, count) should infer supported complex value types");
static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const std::complex<float>*>(nullptr),
                                                              static_cast<const std::complex<float>*>(nullptr))),
                             default_complexf>,
    "partial_load(pointer, sentinel) should keep the same default complex type");
static_assert(std::is_same_v<decltype(std::simd::partial_load(std::declval<std::span<const std::complex<float>>>())), default_complexf>,
    "partial_load(range) should keep the same default complex type");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr))), default_complexf>,
    "unchecked_load(pointer) should infer supported complex value types");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr), std::simd::simd_size_type{})), default_complexf>,
    "unchecked_load(pointer, count) should infer supported complex value types");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr),
                                                                typename default_complexf::mask_type{})),
                             default_complexf>,
    "masked unchecked_load(pointer) should infer supported complex value types");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr),
                                                                static_cast<const std::complex<float>*>(nullptr),
                                                                std::simd::flag_default)),
                             default_complexf>,
    "unchecked_load(pointer, sentinel) should keep the same default complex type");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(std::declval<std::span<const std::complex<float>>>(),
                                                                std::simd::flag_default)),
                             default_complexf>,
    "unchecked_load(range) should keep the same default complex type");

#if defined(__SIZEOF_INT128__)
static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const int128*>(nullptr), std::simd::simd_size_type{})), default_int128>,
    "partial_load(pointer, count) should infer supported extended integer types");
static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const int128*>(nullptr), static_cast<const int128*>(nullptr))), default_int128>,
    "partial_load(pointer, sentinel) should keep the same default extended integer type");
static_assert(std::is_same_v<decltype(std::simd::partial_load(std::declval<std::span<const int128>>())), default_int128>,
    "partial_load(range) should keep the same default extended integer type");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const int128*>(nullptr))), default_int128>,
    "unchecked_load(pointer) should infer supported extended integer types");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const int128*>(nullptr), std::simd::simd_size_type{})), default_int128>,
    "unchecked_load(pointer, count) should infer supported extended integer types");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const int128*>(nullptr),
                                                                static_cast<const int128*>(nullptr),
                                                                std::simd::flag_default)),
                             default_int128>,
    "unchecked_load(pointer, sentinel) should keep the same default extended integer type");
static_assert(std::is_same_v<decltype(std::simd::unchecked_load(std::declval<std::span<const int128>>(), std::simd::flag_default)), default_int128>,
    "unchecked_load(range) should keep the same default extended integer type");
#endif

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES)

static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr))), int4>::value,
    "unchecked_load(pointer) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load(static_cast<const int*>(nullptr))), std::simd::basic_vec<int>>::value,
    "unchecked_load(pointer) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{})), int4>::value,
    "unchecked_load(iterator, count) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load(std::declval<const_int_iter>(), std::simd::simd_size_type{})), std::simd::basic_vec<int>>::value,
    "unchecked_load(iterator, count) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr), mask4{})), int4>::value,
    "unchecked_load(pointer, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::simd::simd_size_type{}, mask4{})), int4>::value,
    "unchecked_load(iterator, count, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr), std::simd::flag_default)), int4>::value,
    "unchecked_load(pointer, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load(static_cast<const int*>(nullptr), static_cast<const int*>(nullptr), std::simd::flag_default)), std::simd::basic_vec<int>>::value,
    "unchecked_load(pointer, sentinel) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::unchecked_load<int4>(std::declval<const_int_iter>(), std::declval<const_int_iter>(), std::simd::flag_default)), int4>::value,
    "unchecked_load(iterator, sentinel) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::unchecked_load(std::declval<std::span<const int, 4>>(), std::simd::flag_default)), std::simd::basic_vec<int>>::value,
    "unchecked_load(range) should provide the standard default vector type");
static_assert(std::is_same<decltype(std::simd::unchecked_load(std::declval<std::span<const int, 4>>(),
                                                              typename std::simd::basic_vec<int>::mask_type{},
                                                              std::simd::flag_default)),
                           std::simd::basic_vec<int>>::value,
    "unchecked_load(range, mask) should provide the standard default vector type");
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
static_assert(std::is_same<decltype(std::simd::unchecked_store(std::declval<const int4&>(), std::declval<std::span<int, 4>>(), mask4{}, std::simd::flag_default)), void>::value,
    "unchecked_store(value, range, mask) should be a public entry point");

#endif

} // namespace

int main() {
    return 0;
}

#include <forge/simd.hpp>
#include <type_traits>

#if defined(FORGE_SIMD_LEGACY_FORGE_FIXED_SIZE_ABI)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
static_assert(std::is_same_v<forge::simd_abi<int, 4>, std::simd::vec<int, 4>::abi_type>);
#else
using abi = forge::simd_fixed_size_abi<4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_FORGE_NATIVE_ABI_TAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
static_assert(std::is_same_v<forge::simd_native_abi<int>, std::simd::vec<int>::abi_type>);
#else
using abi = forge::simd_native_abi_tag<int, 4>;
#endif
#else
#error Missing legacy Forge SIMD probe selector
#endif

int main() {}

#include <simd>

// CC Forge simd backport now covers the full [simd.syn] API surface.
// Verify the feature-test macro IS defined when backport is active.
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && !defined(__cpp_lib_simd)
#error CC Forge simd backport must define __cpp_lib_simd (full wording coverage achieved).
#endif
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && !defined(__cpp_lib_simd_bitops)
#error CC Forge simd backport must define __cpp_lib_simd_bitops.
#endif
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && !defined(__cpp_lib_simd_complex)
#error CC Forge simd backport must define __cpp_lib_simd_complex.
#endif
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && !defined(__cpp_lib_simd_permutations)
#error CC Forge simd backport must define __cpp_lib_simd_permutations.
#endif

#if defined(__cpp_lib_simd)
static_assert(__cpp_lib_simd >= 202606L, "Expected __cpp_lib_simd >= 202606L");
#endif
#if defined(__cpp_lib_simd_bitops)
static_assert(__cpp_lib_simd_bitops >= 202607L, "Expected __cpp_lib_simd_bitops >= 202607L");
#endif
#if defined(__cpp_lib_simd_complex)
static_assert(__cpp_lib_simd_complex >= 202502L, "Expected __cpp_lib_simd_complex >= 202502L");
#endif
#if defined(__cpp_lib_simd_permutations)
static_assert(__cpp_lib_simd_permutations >= 202506L, "Expected __cpp_lib_simd_permutations >= 202506L");
#endif

int main() {
    return 0;
}

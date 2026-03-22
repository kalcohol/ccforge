#include <simd>

// CC Forge simd backport now covers the full [simd.syn] API surface.
// Verify the feature-test macro IS defined when backport is active.
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && !defined(__cpp_lib_simd)
#error CC Forge simd backport must define __cpp_lib_simd (full wording coverage achieved).
#endif

#if defined(__cpp_lib_simd)
static_assert(__cpp_lib_simd >= 202411L, "Expected __cpp_lib_simd >= 202411L");
#endif

int main() {
    return 0;
}

#include <simd>

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED) && defined(__cpp_lib_simd)
#error Forge simd backport must not claim __cpp_lib_simd before full wording coverage.
#endif

int main() {
    return 0;
}

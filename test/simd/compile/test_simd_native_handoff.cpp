#include <simd>

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
#error Forge simd backport must stay disabled when native std::simd is already available.
#endif

int main() {
    std::simd::vec<int, 4> value(1);
    return value[0];
}

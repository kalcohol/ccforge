#include <simd>

#if !defined(FORGE_HAS_NATIVE_SIMD)
#error Forge must mark partial native std::simd as native stand-aside.
#endif

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
#error Forge simd backport must not inject over partial native std::simd.
#endif

constexpr std::simd::vec<int, 4> value(1);
static_assert(value[0] == 1,
    "the partial-native SIMD fixture should preserve its native lane values");

int main() {}

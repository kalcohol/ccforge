#include <linalg>
#include <mdspan>
#include <simd>

#if !defined(FORGE_HAS_NATIVE_SIMD)
#error A partial std::simd root must make Forge stand aside.
#endif
#if !defined(FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS)
#error A partial padded-layout root must make Forge stand aside.
#endif
#if !defined(FORGE_HAS_NATIVE_SUBMDSPAN)
#error A partial std::submdspan root must make Forge stand aside.
#endif
#if !defined(FORGE_HAS_NATIVE_LINALG)
#error A partial std::linalg root must make Forge stand aside.
#endif

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
#error Forge must not inject std::simd over a partial native surface.
#endif
#if defined(FORGE_BACKPORT_LINALG_HPP_INCLUDED)
#error Forge must not inject std::linalg over a partial native surface.
#endif

int main() {
    return 0;
}

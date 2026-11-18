#include <linalg>

#ifndef FORGE_TEST_LINALG_EMPTY_NAMESPACE_INCLUDED
#error "The linalg wrapper must see the empty-namespace fixture"
#endif
#if defined(FORGE_HAS_NATIVE_LINALG) || defined(FORGE_FORCE_LINALG_BACKPORT)
#error "An empty macro-free linalg namespace must select ordinary backport injection"
#endif
#ifndef FORGE_BACKPORT_LINALG_HPP_INCLUDED
#error "Forge must inject linalg over an empty macro-free namespace"
#endif
#if defined(__cpp_lib_mdspan) && !defined(FORGE_LINALG_BACKPORT_ACTIVE)
#error "The linalg backport must be active when mdspan has its feature macro"
#endif

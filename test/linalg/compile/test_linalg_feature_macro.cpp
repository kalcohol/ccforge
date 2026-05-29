// Compile probe: Forge linalg is an experimental subset and intentionally does
// not define the standard __cpp_lib_linalg feature-test macro when injected.

#include <linalg>

#if defined(FORGE_BACKPORT_LINALG_HPP_INCLUDED) && defined(__cpp_lib_linalg)
#error "Forge linalg backport must not define __cpp_lib_linalg yet"
#endif

int main() { return 0; }

// Compile probe: the injected surface reports the working draft feature value.

#if defined(FORGE_TEST_LINALG_FORCE_OLDER_MACRO)
#include <version>
#endif
#include <linalg>

#if defined(FORGE_TEST_LINALG_FORCE_OLDER_MACRO) && \
    (!defined(FORGE_TEST_LINALG_OLDER_MACRO_SEEN) || !defined(FORGE_LINALG_BACKPORT_ACTIVE))
#error "Forced injection must run after the older macro-only linalg header"
#endif

#if defined(FORGE_LINALG_BACKPORT_ACTIVE)
#if !defined(__cpp_lib_linalg) || __cpp_lib_linalg != 202511L
#error "Forge linalg backport must define __cpp_lib_linalg as 202511L"
#endif
#endif

int main() { return 0; }

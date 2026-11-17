#include <linalg>
#ifdef FORGE_EXECUTION_BACKPORT_ACTIVE
#error "Including linalg must not load Forge P2300"
#endif
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS != FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
#error "linalg policy availability must follow native policy objects"
#endif
int main() {}

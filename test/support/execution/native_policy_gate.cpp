// Wrapper check: the header's decision must equal the measured native names.
#include <forge_native_execution.hpp>

#if !defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) || !defined(FORGE_NATIVE_EXECUTION_POLICY_UNSEQ)
#error The wrapper must define both policy capabilities
#endif

#include "native_policy_config.hpp"

int main() {}

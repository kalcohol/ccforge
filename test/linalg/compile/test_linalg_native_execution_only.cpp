#include <linalg>
#include "native_policy_config.hpp"

#ifdef FORGE_EXECUTION_BACKPORT_ACTIVE
#error "Including linalg must not load Forge P2300"
#endif
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS != FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
#error "linalg policy availability must follow native policy objects"
#endif

int main() {
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
    double input[]{1.0}, output[]{0.0};
    std::mdspan in(input, std::extents<int, 1>{});
    std::mdspan out(output, std::extents<int, 1>{});
    std::linalg::copy(std::execution::seq, in, out);
    std::linalg::copy(std::execution::par, in, out);
    std::linalg::copy(std::execution::par_unseq, in, out);
#if FORGE_NATIVE_EXECUTION_POLICY_UNSEQ
    std::linalg::copy(std::execution::unseq, in, out);
#endif
    return output[0] != input[0];
#endif
}

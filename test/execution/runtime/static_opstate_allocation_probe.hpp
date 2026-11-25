#pragma once

#include "static_opstate_sanitizer.hpp"

#if defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
#include <cstdio>

int main() {
    std::fprintf(stderr,
        "Skipped: ThreadSanitizer defines the global allocation replacements used by this test.\n");
    return 77;
}
#else

#include <gtest/gtest.h>
#include <execution>

#include "global_allocation_probe.hpp"

namespace allocation_probe {
struct calibration_callback {
    // Match the pointer-sized when_all callback without doing any work.
    void* state = nullptr;
    void operator()() const noexcept {}
};

std::size_t allocations_per_registration() {
    std::inplace_stop_source source;
    const auto allocations = allocation_probe::measure([&] {
        std::inplace_stop_callback<calibration_callback> callback(
            source.get_token(), calibration_callback{});
    });
    EXPECT_EQ(allocations, 0u);
    return allocations;
}

} // namespace allocation_probe

#endif // defined(FORGE_TEST_STATIC_OPSTATE_TSAN)

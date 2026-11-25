#pragma once

#include <execution>

#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE)
namespace test_bulk {

// Standard-side test seam for policy-free serial bulk on lanes without policy objects.
inline constexpr auto bulk = std::execution::__forge_bulk::__serial_bulk_t<false, true>{};
inline constexpr auto bulk_chunked = std::execution::__forge_bulk::__serial_bulk_t<true>{};
inline constexpr auto bulk_unchunked = std::execution::__forge_bulk::__serial_bulk_t<false>{};

} // namespace test_bulk
#endif

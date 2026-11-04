#ifndef FORGE_TEST_HIVE_EXPLICIT_INT_HPP
#define FORGE_TEST_HIVE_EXPLICIT_INT_HPP

#include <concepts>
#include <type_traits>

// Non-const lvalue conversion makes this a container-compatible proxy and
// permits int construction/assignment.  The const form deliberately has no
// conversion, so assignable_from's common-reference requirement is false.
struct construct_only_proxy {
    int value;
    operator int() & { return value; }
};

static_assert(std::convertible_to<construct_only_proxy&, int>);
static_assert(std::constructible_from<int, construct_only_proxy&>);
static_assert(!std::assignable_from<int&, construct_only_proxy&>);

#endif

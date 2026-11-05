#include <version>
#include <hive>
#include <cpp26/hive.hpp>
#include <hive>

#if HIVE_EXPECT_NATIVE && !defined(FORGE_FORCE_HIVE_BACKPORT)
#ifndef FORGE_HAS_NATIVE_HIVE
#error missing native stand-aside signal
#endif
#ifdef FORGE_BACKPORT_HIVE_HPP_INCLUDED
#error injected backport on partial or complete native roots
#endif
#else
#ifndef FORGE_BACKPORT_HIVE_HPP_INCLUDED
#error no native roots must enable the backport
#endif
#if __cpp_lib_hive != 202502L
#error wrong backport feature macro
#endif
#endif

int main() {
#if !HIVE_EXPECT_NATIVE || HIVE_FIXTURE_CASE == 6
    std::hive<int> h;
    h.emplace(42);
    return *h.begin() == 42 ? 0 : 1;
#else
    return 0;
#endif
}

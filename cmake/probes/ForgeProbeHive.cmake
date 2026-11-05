# std::hive (P0447). Run at the consumer's configured language standard.
# An empty installed header is NONE, not PARTIAL. Each namespace root is
# checked independently, without requiring an instantiable hive<int>.
check_cxx_source_compiles("
    #include <version>
    #include <hive>
    #include <array>
    #include <compare>
    #include <concepts>
    #include <iterator>
    #include <memory>
    #include <memory_resource>
    #include <utility>
    #if !defined(__cpp_lib_hive) || __cpp_lib_hive < 202502L
    #error incomplete hive surface
    #endif
    using H = std::hive<int>;
    static_assert(std::bidirectional_iterator<H::iterator>);
    static_assert(std::bidirectional_iterator<H::const_iterator>);
    static_assert(std::three_way_comparable<H::iterator, std::strong_ordering>);
    static_assert(std::same_as<std::pmr::hive<int>,
        std::hive<int, std::pmr::polymorphic_allocator<int>>>);
    int main() {
        auto limits = H::block_capacity_default_limits();
        auto hard = H::block_capacity_hard_limits();
        H h(limits);
        h.reserve(32);
        auto i = h.emplace(3);
        auto p = std::addressof(*i);
        i = h.get_iterator(p);
        const H& c = h;
        auto ci = c.get_iterator(p);
        (void)(i <=> ci);
        ++i;
        --i;
        (void)*c.rbegin();
        h.insert(2);
        h.erase(i);
        std::array<int, 3> values{3, 1, 1};
        H ranged(std::from_range, values);
        h.insert_range(values);
        ranged.assign_range(values);
        H copied(ranged);
        H moved(std::move(copied));
        copied = moved;
        copied = std::move(moved);
        h.splice(ranged);
        h.sort();
        h.unique();
        std::erase(h, 3);
        std::erase_if(h, [](int v) { return v == 2; });
        h.reshape(limits);
        h.shrink_to_fit();
        h.trim_capacity();
        h.trim_capacity(h.capacity());
        h.swap(copied);
        std::pmr::monotonic_buffer_resource resource;
        std::pmr::hive<int> pmr{&resource};
        pmr.emplace(7);
        (void)h.get_allocator();
        h.clear();
        return static_cast<int>(h.size() + pmr.size() + hard.min +
            h.max_size());
    }
" FORGE_HIVE_FULL)

check_cxx_source_compiles("
    #include <version>
    #if __has_include(<hive>)
    #include <hive>
    #endif
    #ifndef __cpp_lib_hive
    #error no hive feature macro
    #endif
    int main() { return 0; }
" FORGE_HIVE_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <hive>
    using probe = std::hive_limits;
    int main() { probe* p = nullptr; (void)p; }
" FORGE_HIVE_PARTIAL_LIMITS)
check_cxx_source_compiles("
    #include <hive>
    template<template<class, class> class> struct accepts_hive {};
    using probe = accepts_hive<std::hive>;
    int main() { probe* p = nullptr; (void)p; }
" FORGE_HIVE_PARTIAL_TEMPLATE)
check_cxx_source_compiles("
    #include <hive>
    template<template<class> class> struct accepts_pmr_hive {};
    using probe = accepts_pmr_hive<std::pmr::hive>;
    int main() { probe* p = nullptr; (void)p; }
" FORGE_HIVE_PARTIAL_PMR)
set(FORGE_HIVE_PARTIAL FALSE)
if(FORGE_HIVE_PARTIAL_MACRO OR FORGE_HIVE_PARTIAL_LIMITS
        OR FORGE_HIVE_PARTIAL_TEMPLATE OR FORGE_HIVE_PARTIAL_PMR)
    set(FORGE_HIVE_PARTIAL TRUE)
endif()
_forge_decide("std::hive" HIVE FORGE_HIVE_FULL FORGE_HIVE_PARTIAL)

#include <version>
#include <hive>
#include <memory_resource>

#if !defined(__cpp_lib_hive) || __cpp_lib_hive < 202502L
#error "Including <hive> must expose its feature-test macro"
#endif

#if defined(CCFORGE_CONSUMER_STD_ONLY) && __has_include(<forge/execution.hpp>)
#error "forge::std must not expose extension headers"
#endif

int main() {
    std::hive<int> values;
    values.reserve(32);
    const auto retained = values.insert(7);
    const auto removed = values.insert(9);
    const int* address = &*retained;
    values.erase(removed);
    values.insert(11);
    if (values.size() != 2 || &*retained != address || *retained != 7
            || values.get_iterator(address) != retained) {
        return 1;
    }
    values.sort();
    if (*values.begin() != 7) {
        return 2;
    }

    std::pmr::monotonic_buffer_resource memory;
    std::pmr::hive<int> allocated{&memory};
    allocated.insert(42);
    return allocated.size() == 1 && *allocated.begin() == 42 ? 0 : 3;
}

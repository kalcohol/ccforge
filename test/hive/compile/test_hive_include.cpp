#include <hive>

#include <memory>
#include <memory_resource>
#include <type_traits>

#if !defined(__cpp_lib_hive) || __cpp_lib_hive != 202502L
#  error "<hive> must define __cpp_lib_hive as 202502L"
#endif

static_assert(std::is_same_v<std::hive<int>::value_type, int>);
static_assert(std::is_same_v<std::pmr::hive<int>,
              std::hive<int, std::pmr::polymorphic_allocator<int>>>);

int main() {
    std::hive<int> hive;
    hive.emplace(42);
    return *hive.begin() == 42 ? 0 : 1;
}

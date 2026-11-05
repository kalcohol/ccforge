#include <version>
#include <hive>

#include <type_traits>

#if !defined(__cpp_lib_hive) || __cpp_lib_hive != 202502L
#  error "<hive> must define __cpp_lib_hive as 202502L after <version>"
#endif

static_assert(std::is_same_v<std::hive<int>::value_type, int>);
static_assert(std::hive_limits{2, 8}.min == 2);

int main() {
    std::hive<int> hive;
    hive.emplace(1);
    return hive.size() == 1 ? 0 : 1;
}

#include <version>
#include <hive>

#include <numeric>
#include <vector>

std::vector<int> hive_other_tu_transform(std::hive<int>& values);

int main() {
#if !defined(__cpp_lib_hive) || __cpp_lib_hive != 202502L
#error "the backport feature macro must agree across translation units"
#endif
    std::hive<int> values{4, 1, 3, 2};
    const auto transformed = hive_other_tu_transform(values);
    if (transformed != std::vector<int>{1, 2, 3, 6}) {
        return 1;
    }
    return std::accumulate(values.begin(), values.end(), 0) == 12 ? 0 : 2;
}

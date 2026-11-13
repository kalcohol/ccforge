#include <hive>
#include <ranges>
#include <vector>
#include "hive_explicit_int.hpp"

int main() {
    std::vector<construct_only_proxy> input{{1}, {2}};
    std::hive<int> hive(std::from_range, input);
    return hive.size() == 2 ? 0 : 1;
}

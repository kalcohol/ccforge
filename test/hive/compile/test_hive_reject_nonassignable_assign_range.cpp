#include <hive>
#include <vector>
#include "hive_explicit_int.hpp"

int main() {
    std::vector<construct_only_proxy> input{{1}, {2}};
    std::hive<int> hive;
    hive.assign_range(input);
}

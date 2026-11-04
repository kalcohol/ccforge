#include <hive>

#include <vector>

std::vector<int> hive_other_tu_transform(std::hive<int>& values) {
    values.emplace(6);
    values.erase(values.begin());
    values.sort();
    return {values.begin(), values.end()};
}

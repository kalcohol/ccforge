#include <hive>
#include <ranges>
#include <vector>

int main() {
    std::vector<int> input{1, 2, 3};
    std::hive<int> hive(std::from_range, input);
    return hive.size() == 3 ? 0 : 1;
}

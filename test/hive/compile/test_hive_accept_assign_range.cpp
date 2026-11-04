#include <hive>
#include <vector>

int main() {
    std::vector<int> input{1, 2};
    std::hive<int> hive;
    hive.assign_range(input);
    return hive.size() == 2 ? 0 : 1;
}

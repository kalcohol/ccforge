#include <hive>
#include <ranges>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> input{"not", "an", "integer"};
    std::hive<int> hive(std::from_range, input);
}

#include <hive>
#include <vector>

struct not_an_allocator {
    using value_type = int;
};

int main() {
    std::vector<int> input{1, 2, 3};
    std::hive hive(input.begin(), input.end(), not_an_allocator{});
}

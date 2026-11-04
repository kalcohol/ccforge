#include <hive>
#include <memory>
#include <vector>

int main() {
    std::vector<int> input{1, 2, 3};
    std::hive a(input.begin(), input.end());
    std::hive b(input.begin(), input.end(), std::allocator<int>{});
    return a.size() == b.size() ? 0 : 1;
}

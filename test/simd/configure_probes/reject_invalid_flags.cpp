#include <simd>

int main() {
    [[maybe_unused]] std::simd::flags<int> invalid{};
    return 0;
}

#include <simd>

int main() {
    std::simd::vec<int, 4> ints(1);
    std::simd::vec<long long, 4> widened(ints);
    return static_cast<int>(widened[0]);
}

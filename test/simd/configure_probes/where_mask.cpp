#include <simd>

int main() {
    using M = std::simd::mask<int, 4>;

    M cond(0b0101u);
    M target(0u);
    M other(0b1111u);
    std::simd::where(cond, target) = other;
    return target[0] ? 0 : 1;
}

#include <simd>

int main() {
    using V = std::simd::vec<int, 4>;
    using M = std::simd::mask<int, 4>;

    V v([](auto i) { return static_cast<int>(decltype(i)::value + 1); });
    M m(0b0101u);
    std::simd::where(m, v) %= 3;
    std::simd::where(m, v) &= 7;
    std::simd::where(m, v) |= 1;
    std::simd::where(m, v) ^= 2;
    return v[0];
}

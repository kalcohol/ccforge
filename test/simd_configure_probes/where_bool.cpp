#include <simd>

int main() {
    using V = std::simd::vec<int, 4>;

    V v([](auto i) { return static_cast<int>(decltype(i)::value); });
    std::simd::where(true, v) = 1;
    std::simd::where(false, v) = 2;
    return v[0];
}

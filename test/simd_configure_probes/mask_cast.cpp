#include <simd>

int main() {
    using M1 = std::simd::mask<int, 4>;
    using M2 = std::simd::mask<long long, 4>;

    M1 value(0b0101u);
    (void)std::simd::simd_cast<M2>(value);
    (void)std::simd::static_simd_cast<M1>(value);
    return 0;
}

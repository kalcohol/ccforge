#include <simd>

int main() {
    using int4 = std::simd::vec<int, 4>;

    static_assert(std::simd::alignment_v<int4, float> == alignof(float) * 4);
    static_assert(std::simd::alignment_v<int4, unsigned long long> == alignof(unsigned long long) * 4);
    return 0;
}

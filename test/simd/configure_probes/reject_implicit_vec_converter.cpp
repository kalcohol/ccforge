#include <simd>

int main() {
    std::simd::vec<int, 4> ints(1);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<long long, 4> converted = ints;
#else
    std::simd::vec<float, 4> converted = ints;
#endif
    (void)converted;
    return 0;
}

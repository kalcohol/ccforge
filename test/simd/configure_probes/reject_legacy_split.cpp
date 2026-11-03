#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    (void)std::simd::chunk<std::simd::vec<int, 2>>(value);
#else
    (void)std::simd::split<std::simd::vec<int, 2>>(value);
#endif
}

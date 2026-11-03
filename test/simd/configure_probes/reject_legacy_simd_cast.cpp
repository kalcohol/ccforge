#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    (void)std::simd::vec<float, 4>(value, std::simd::flag_convert);
#else
    (void)std::simd::simd_cast<std::simd::vec<float, 4>>(value);
#endif
}

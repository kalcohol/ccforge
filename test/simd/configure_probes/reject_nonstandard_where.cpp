#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
    std::simd::mask<int, 4> mask_value(0b0101u);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    value = std::simd::select(mask_value, value, value);
#else
    std::simd::where(mask_value, value) = value;
#endif
    return value[0];
}

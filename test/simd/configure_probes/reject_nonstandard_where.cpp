#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
    std::simd::mask<int, 4> mask_value(0b0101u);
    std::simd::where(mask_value, value) = value;
    return value[0];
}

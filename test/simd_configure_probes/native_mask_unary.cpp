#include <simd>

int main() {
    std::simd::mask<float> float_mask(0b0101u);
    std::simd::mask<double> double_mask(0b01u);
    std::simd::mask<unsigned int> uint_mask(0b0011u);
    auto positive = +float_mask;
    auto negative = -double_mask;
    auto inverted = ~uint_mask;
    return static_cast<int>(positive[0] + negative[0] + inverted[0]);
}

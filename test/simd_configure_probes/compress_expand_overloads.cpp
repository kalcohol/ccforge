#include <simd>

int main() {
    using V = std::simd::vec<int, 4>;
    using M = std::simd::mask<int, 4>;

    V values(1);
    M selected(0b0101u);
    auto packed = std::simd::compress(values, selected, 7);
    auto expanded = std::simd::expand(values, selected, values);
    auto packed_mask = std::simd::compress(selected, selected);
    auto expanded_mask = std::simd::expand(selected, selected, selected);
    auto blended = std::simd::select(selected, 1, 2);
    return packed[0] + expanded[0] + (packed_mask[0] ? 1 : 0) + (expanded_mask[0] ? 1 : 0) + blended[0];
}

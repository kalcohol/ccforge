#include <simd>

int main() {
    using uint4 = std::simd::vec<unsigned, 4>;

    uint4 values(1u);
    auto swapped = std::simd::byteswap(values);
    auto counts = std::simd::popcount(values);
    auto leading = std::simd::countl_zero(values);
    auto rotated = std::simd::rotl(values, 1);
    auto masked = std::simd::has_single_bit(values);

    return static_cast<int>(swapped[0] + static_cast<unsigned>(counts[0] + leading[0] + rotated[0] + (masked[0] ? 1 : 0)));
}

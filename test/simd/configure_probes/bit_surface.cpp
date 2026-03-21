#include <simd>
#include <type_traits>

int main() {
    using uint4 = std::simd::vec<unsigned, 4>;
    using int4 = std::simd::vec<int, 4>;
    using uchar4 = std::simd::vec<unsigned char, 4>;
    using schar4 = std::simd::vec<signed char, 4>;

    uint4 values(1u);
    int4 shifts(1);
    uchar4 small_values(static_cast<unsigned char>(1));

    static_assert(std::is_same_v<decltype(std::simd::popcount(values)), int4>);
    static_assert(std::is_same_v<decltype(std::simd::countl_zero(small_values)), schar4>);
    static_assert(std::is_same_v<decltype(std::simd::rotl(values, shifts)), uint4>);

    auto swapped = std::simd::byteswap(values);
    auto counted = std::simd::popcount(values);
    auto leading = std::simd::countl_zero(values);
    auto rotated = std::simd::rotl(values, shifts);
    auto floored = std::simd::bit_floor(values);

    return static_cast<int>(swapped[0] + rotated[0] + floored[0] + static_cast<unsigned>(counted[0] + leading[0]));
}

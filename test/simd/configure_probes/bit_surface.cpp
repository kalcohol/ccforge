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
    auto reversed = std::simd::bit_reverse(values);
    auto shifted_left = std::simd::shl(values, shifts);
    auto shifted_right = std::simd::shr(values, 1);
    auto rotated = std::simd::rotl(values, shifts);
    auto repeated = std::simd::bit_repeat(values, shifts);
    auto compressed = std::simd::bit_compress(values, 3);
    auto expanded = std::simd::bit_expand(values, 1);
    auto floored = std::simd::bit_floor(values);

    return static_cast<int>(
        swapped[0] + reversed[0] + shifted_left[0] + shifted_right[0] +
        rotated[0] + repeated[0] + compressed[0] + expanded[0] +
        floored[0] + static_cast<unsigned>(counted[0] + leading[0]));
}

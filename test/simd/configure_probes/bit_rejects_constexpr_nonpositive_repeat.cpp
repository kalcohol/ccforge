#include <simd>

constexpr auto repeated =
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::bit_repeat(std::simd::vec<unsigned, 4>(1u), 1);
#else
    std::simd::bit_repeat(std::simd::vec<unsigned, 4>(1u), -1);
#endif

int main() {
    return static_cast<int>(repeated[0]);
}

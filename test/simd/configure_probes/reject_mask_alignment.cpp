#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    constexpr auto alignment = std::simd::alignment_v<std::simd::vec<int, 4>, int>;
#elif defined(FORGE_SIMD_MASK_ALIGNMENT_CASE) && FORGE_SIMD_MASK_ALIGNMENT_CASE == 0
    constexpr auto alignment = std::simd::alignment_v<std::simd::mask<int, 4>, int>;
#elif defined(FORGE_SIMD_MASK_ALIGNMENT_CASE) && FORGE_SIMD_MASK_ALIGNMENT_CASE == 1
    constexpr auto alignment = std::simd::alignment_v<std::simd::vec<int, 4>, bool>;
#elif defined(FORGE_SIMD_MASK_ALIGNMENT_CASE)
#error Invalid alignment condition
#else
    constexpr auto alignment = std::simd::alignment_v<std::simd::mask<int, 4>, bool>;
#endif
    return static_cast<int>(alignment);
}

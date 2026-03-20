#include <simd>
#include <type_traits>

int main() {
#if defined(FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER)
    static_assert(std::is_constructible<std::simd::vec<float, 4>, std::simd::vec<int, 4>>::value,
        "Implicit vector conversion should compile when the compile probe passes");
#else
    static_assert(!std::is_constructible<std::simd::vec<float, 4>, std::simd::vec<int, 4>>::value,
        "Implicit vector conversion should be disabled when the compile probe fails");
#endif
#if defined(FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR)
    static_assert(std::is_constructible<std::simd::vec<float, 4>, std::simd::vec<int, 4>, std::simd::flags<std::simd::convert_flag>>::value,
        "Flag-convert constructor should remain available");
#endif
    return 0;
}

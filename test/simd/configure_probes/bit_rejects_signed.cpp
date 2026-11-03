#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    using int4 = std::simd::vec<unsigned, 4>;
#else
    using int4 = std::simd::vec<int, 4>;
#endif
    auto values = std::simd::bit_floor(int4{});
    return values[0];
}

#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<float, 4> values(1.0f);
#else
    std::simd::vec<int, 4> values(1);
#endif
    auto bad = std::simd::sin(values);
    return bad[0];
}

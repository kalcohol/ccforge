#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<float, 4> values(1.0f);
#else
    std::simd::vec<float, 4> values(1);
#endif
    return static_cast<int>(values[0]);
}

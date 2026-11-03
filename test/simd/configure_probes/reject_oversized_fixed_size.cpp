#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<int, 64> value(0);
#else
    std::simd::vec<int, 65> value(0);
#endif
    return value[0];
}

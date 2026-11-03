#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<double, 2> value(1.0);
#else
    std::simd::vec<long double, 2> value(1.0L);
#endif
    return static_cast<int>(value[0]);
}

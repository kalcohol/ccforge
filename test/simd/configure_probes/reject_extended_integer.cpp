#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::basic_vec<long long> value;
#else
    std::simd::basic_vec<__int128> value;
#endif
    return static_cast<int>(value[0]);
}

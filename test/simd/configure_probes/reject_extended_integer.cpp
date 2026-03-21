#include <simd>

int main() {
#if defined(__SIZEOF_INT128__)
    std::simd::basic_vec<__int128> value;
    return static_cast<int>(value[0]);
#else
    return 0;
#endif
}

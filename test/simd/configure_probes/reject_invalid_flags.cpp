#include <simd>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    [[maybe_unused]] std::simd::flags<std::simd::convert_flag> invalid{};
#else
    [[maybe_unused]] std::simd::flags<int> invalid{};
#endif
    return 0;
}

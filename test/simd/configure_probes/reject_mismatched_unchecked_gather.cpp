#include <simd>

#include <array>

int main() {
    std::array<int, 8> input{};
    const std::simd::vec<int, 4> indices(0);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    const auto values = std::simd::unchecked_gather_from<std::simd::vec<int, 4>>(input, indices);
#else
    const auto values = std::simd::unchecked_gather_from<std::simd::vec<int, 8>>(input, indices);
#endif
    return values[0];
}

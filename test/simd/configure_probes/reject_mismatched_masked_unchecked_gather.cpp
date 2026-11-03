#include <simd>

#include <array>

int main() {
    std::array<int, 8> input{};
    const std::simd::vec<int, 4> indices(0);
    const std::simd::mask<int, 4> selected(true);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    const auto values = std::simd::unchecked_gather_from<std::simd::vec<int, 4>>(input, selected, indices);
#else
    const auto values = std::simd::unchecked_gather_from<std::simd::vec<int, 8>>(input, selected, indices);
#endif
    return values[0];
}

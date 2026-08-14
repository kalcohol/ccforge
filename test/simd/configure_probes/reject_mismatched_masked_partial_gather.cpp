#include <simd>

#include <array>

int main() {
    std::array<int, 8> input{};
    const std::simd::vec<int, 4> indices(0);
    const std::simd::mask<int, 4> selected(true);
    const auto values = std::simd::partial_gather_from<std::simd::vec<int, 8>>(input, selected, indices);
    return values[0];
}

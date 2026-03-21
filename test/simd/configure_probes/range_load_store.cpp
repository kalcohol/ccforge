#include <simd>

#include <array>
#include <span>

int main() {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};
    std::span<const int, 4> input_view(input);
    std::span<int, 4> output_view(output);
    std::simd::vec<int, 4> values(input_view);
    auto loaded = std::simd::partial_load<std::simd::vec<int, 4>>(input_view);
    std::simd::partial_store(loaded, output_view);
    return values[0] + output[3];
}

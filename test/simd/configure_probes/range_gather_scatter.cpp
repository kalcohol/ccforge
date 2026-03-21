#include <simd>

#include <array>
#include <span>

int main() {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{0, 0, 0, 0, 0, 0, 0, 0}};
    std::span<const int, 8> input_view(input);
    std::span<int, 8> output_view(output);
    std::simd::vec<int, 4> indices([](auto lane) {
        constexpr int values[] = {1, 4, 5, 7};
        return values[decltype(lane)::value];
    });
    auto gathered = std::simd::partial_gather_from<std::simd::vec<int, 4>>(input_view, indices);
    std::simd::partial_scatter_to(gathered, output_view, indices);
    return output[7];
}

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
    const std::simd::mask<int, 4> selected(0b0101u);
    auto gathered = std::simd::partial_gather_from(input_view, indices);
    auto masked = std::simd::unchecked_gather_from(input_view, selected, indices);
    static_assert(std::is_same_v<decltype(gathered), std::simd::vec<int, 4>>);
    static_assert(std::is_same_v<decltype(masked), std::simd::vec<int, 4>>);
    std::simd::partial_scatter_to(gathered, output_view, indices);
    std::simd::unchecked_scatter_to(masked, output_view, selected, indices);
    return output[5] + output[7];
}

#include <simd>

#include <array>
#include <span>

int main() {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};
    std::span<const int, 4> input_view(input);
    std::span<int, 4> output_view(output);
    auto loaded = std::simd::partial_load(input_view);
    auto unchecked = std::simd::unchecked_load(input_view, std::simd::flag_default);
    static_assert(std::is_same_v<decltype(loaded), std::simd::basic_vec<int>>);
    static_assert(std::is_same_v<decltype(unchecked), std::simd::basic_vec<int>>);
    std::simd::partial_store(loaded, output_view);
    return loaded[0] + unchecked[0] + output[3];
}

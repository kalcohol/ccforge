#include <simd>

#include <array>
#include <span>

int main() {
    constexpr auto width = static_cast<std::size_t>(std::simd::basic_vec<int>::size);
    std::array<int, width> input{{1}};
    std::array<int, width> output{};
    std::span<const int, width> input_view(input);
    std::span<int, width> output_view(output);
    auto loaded = std::simd::partial_load(input_view);
    auto unchecked = std::simd::unchecked_load(input_view, std::simd::flag_default);
    static_assert(std::is_same_v<decltype(loaded), std::simd::basic_vec<int>>);
    static_assert(std::is_same_v<decltype(unchecked), std::simd::basic_vec<int>>);
    std::simd::partial_store(loaded, output_view);
    return loaded[0] + unchecked[0] + output[0];
}

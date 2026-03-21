#include <simd>

#include <array>

int main() {
    std::array<int, 8> data{{10, 11, 12, 13, 14, 15, 16, 17}};
    const std::simd::vec<int, 4> indices([](auto lane) {
        constexpr int values[] = {6, 0, 3, 7};
        return values[decltype(lane)::value];
    });
    const std::simd::mask<int, 4> selected(0b0101u);
    std::simd::vec<int, 4> values{};

    auto gathered = std::simd::partial_gather_from<std::simd::vec<int, 4>>(
        data.data(),
        std::simd::simd_size_type{8},
        indices,
        selected);
    auto unchecked = std::simd::unchecked_gather_from<std::simd::vec<int, 4>>(
        data.data(),
        indices,
        selected);
    std::simd::partial_scatter_to(values, data.data(), std::simd::simd_size_type{8}, indices, selected);
    std::simd::unchecked_scatter_to(values, data.data(), indices, selected);
    return gathered[0] + unchecked[0];
}

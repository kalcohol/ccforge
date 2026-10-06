#include <mdspan>
#include <tuple>

int main() {
    const std::dextents<int, 1> source(8);
    // Runtime emptiness does not replace a constant zero stride with unit stride.
    const auto slices = std::canonical_slices(
        source, std::range_slice{2, 2, std::cw<0>});
    return std::get<0>(slices).extent;
}

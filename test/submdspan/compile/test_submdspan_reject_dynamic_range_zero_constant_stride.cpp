#include <mdspan>
#include <tuple>

int main() {
    const std::dextents<int, 1> source(8);
    // A constant zero stride violates the Mandate even with a dynamic span.
    const auto slices = std::canonical_slices(
        source, std::range_slice{0, 8, std::cw<0>});
    return std::get<0>(slices).extent;
}

#include <mdspan>
#include <tuple>

int main() {
    const std::dextents<int, 1> source(8);
    // Runtime emptiness does not replace a constant zero stride with unit stride.
    const auto slices = std::canonical_slices(
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
        source, std::range_slice{2, 2, std::cw<1>});
#else
        source, std::range_slice{2, 2, std::cw<0>});
#endif
    return std::get<0>(slices).extent;
}

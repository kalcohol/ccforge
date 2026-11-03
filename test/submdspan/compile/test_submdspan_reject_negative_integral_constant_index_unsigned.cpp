#include <mdspan>
#include <type_traits>

void check_negative_integral_constant_index() {
    const std::dextents<unsigned, 1> source(4);
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
    (void)std::canonical_slices(source, std::integral_constant<int, 0>{});
#else
    (void)std::canonical_slices(source, std::integral_constant<int, -1>{});
#endif
}

int main() { return 0; }

#include <mdspan>
#include <type_traits>

void check_negative_integral_constant_index() {
    const std::dextents<unsigned, 1> source(4);
    (void)std::canonical_slices(source, std::integral_constant<int, -1>{});
}

int main() { return 0; }

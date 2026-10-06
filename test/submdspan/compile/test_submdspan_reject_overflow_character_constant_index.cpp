#include <mdspan>
#include <cstdint>

void check_overflow_character_constant_index() {
    const std::dextents<std::uint8_t, 1> source(4);
    (void)std::canonical_slices(source, std::cw<char16_t{256}>);
}

int main() { return 0; }

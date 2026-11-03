#include <mdspan>
#include <cstdint>

void check_overflow_constant_index() {
    const std::dextents<std::int8_t, 1> source(4);
    // Without the Mandate, narrowing turns this constant into the valid index zero.
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
    (void)std::canonical_slices(source, std::cw<0>);
#else
    (void)std::canonical_slices(source, std::cw<256>);
#endif
}

int main() { return 0; }

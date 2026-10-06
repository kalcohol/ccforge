#include <mdspan>
#include <cstdint>

void check_underflow_constant_index() {
    const std::dextents<std::int8_t, 1> source(4);
    // Static emptiness cannot bypass the stride's representability Mandate.
    (void)std::canonical_slices(source, std::range_slice{std::cw<2>, std::cw<2>, std::cw<-256>});
}

int main() { return 0; }

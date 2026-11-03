#include <mdspan>
#include <cstdint>

struct oversized_constant_index {
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
    static constexpr unsigned value = 0;
#else
    static constexpr unsigned value = 256;
#endif
    constexpr operator unsigned() const noexcept { return value; }
};

void check_overflow_custom_constant_index() {
    const std::dextents<std::uint8_t, 1> source(4);
    (void)std::canonical_slices(source, oversized_constant_index{});
}

int main() { return 0; }

#include <simd>

constexpr bool include_surface_produces_expected_values() {
    const std::simd::vec<int, 4> value(1);
    const auto mask = value == value;
    return std::simd::all_of(mask);
}

static_assert(include_surface_produces_expected_values(),
    "including <simd> alone should expose constexpr vector comparisons and reductions");

int main() {}

#include <mdspan>
#include <cstddef>
#include <cstdint>
#include <limits>

template<std::size_t Padding, class LeftExtents, class RightExtents = LeftExtents>
constexpr bool complete_mappings =
    sizeof(typename std::layout_left_padded<Padding>::template mapping<LeftExtents>) > 0 &&
    sizeof(typename std::layout_right_padded<Padding>::template mapping<RightExtents>) > 0;

using scalar = std::extents<std::int8_t>;
using vector = std::extents<std::int8_t, 127>;
using dynamic_vector = std::extents<std::int8_t, std::dynamic_extent>;
static_assert(complete_mappings<127, scalar>);
static_assert(complete_mappings<8, vector>);
static_assert(complete_mappings<127, dynamic_vector>);
static_assert(complete_mappings<0, std::extents<std::int8_t, 127, 1>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, 120, 1>, std::extents<std::int8_t, 1, 120>>);
static_assert(complete_mappings<std::dynamic_extent,
    std::extents<std::int8_t, 127, 1>, std::extents<std::int8_t, 1, 127>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, std::dynamic_extent, 127>,
    std::extents<std::int8_t, 127, std::dynamic_extent>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, 120, std::dynamic_extent>,
    std::extents<std::int8_t, std::dynamic_extent, 120>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, 61, 2, std::dynamic_extent>,
    std::extents<std::int8_t, std::dynamic_extent, 2, 61>>);
static_assert(complete_mappings<8, std::dextents<std::int8_t, 3>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, 0, 127>, std::extents<std::int8_t, 127, 0>>);
static_assert(complete_mappings<8,
    std::extents<std::int8_t, 60, 60, 0>, std::extents<std::int8_t, 0, 60, 60>>);

// Zero factors must be considered before any overflowing intermediate product.
constexpr auto size_max = (std::numeric_limits<std::size_t>::max)();
static_assert(complete_mappings<8,
    std::extents<std::size_t, size_max - 7, 2, 0>,
    std::extents<std::size_t, 0, 2, size_max - 7>>);

int main() {}

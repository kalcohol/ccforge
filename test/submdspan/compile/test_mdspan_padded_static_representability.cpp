#include <mdspan>
#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(FORGE_TEST_PADDED_RIGHT) || !defined(FORGE_TEST_PADDED_REJECT)
#  error "The padded static probe requires an explicit layout and polarity"
#endif

#if FORGE_TEST_PADDED_RIGHT
template<class I, std::size_t Boundary, std::size_t Other>
using extents_t = std::extents<I, Other, Boundary>;
template<std::size_t Padding, class E>
using mapping_t = typename std::layout_right_padded<Padding>::template mapping<E>;
#else
template<class I, std::size_t Boundary, std::size_t Other>
using extents_t = std::extents<I, Boundary, Other>;
template<std::size_t Padding, class E>
using mapping_t = typename std::layout_left_padded<Padding>::template mapping<E>;
#endif

#if defined(FORGE_TEST_PADDED_STRIDE)
using mapping_type = mapping_t<8, extents_t<std::int8_t,
    FORGE_TEST_PADDED_REJECT ? 127 : 120, 1>>;
#elif defined(FORGE_TEST_PADDED_PARTIAL_STRIDE)
using mapping_type = mapping_t<8, extents_t<std::int8_t,
    FORGE_TEST_PADDED_REJECT ? 127 : 120, std::dynamic_extent>>;
#elif defined(FORGE_TEST_PADDED_PRODUCT)
using mapping_type = mapping_t<8, extents_t<std::int8_t,
    FORGE_TEST_PADDED_REJECT ? 61 : 56, 2>>;
#elif defined(FORGE_TEST_PADDED_SIZE_ROUND)
constexpr auto size_max = (std::numeric_limits<std::size_t>::max)();
using mapping_type = mapping_t<8, extents_t<std::size_t,
    size_max - (FORGE_TEST_PADDED_REJECT ? 1 : 7), 1>>;
#elif defined(FORGE_TEST_PADDED_SIZE_PRODUCT)
constexpr auto size_max = (std::numeric_limits<std::size_t>::max)();
using mapping_type = mapping_t<8, extents_t<std::size_t,
    size_max / 2 - (FORGE_TEST_PADDED_REJECT ? 0 : 7), 2>>;
#elif defined(FORGE_TEST_PADDED_INDEX_PRODUCT)
using mapping_type = mapping_t<std::dynamic_extent, extents_t<std::int8_t,
    FORGE_TEST_PADDED_REJECT ? 64 : 63, 2>>;
#elif defined(FORGE_TEST_PADDED_PADDING)
using mapping_type = mapping_t<FORGE_TEST_PADDED_REJECT ? 128 : 127,
    std::extents<std::int8_t>>;
#else
#  error "Select one padded static representability case"
#endif

// Instantiate only the class: no construction or access of an invalid mapping.
static_assert(sizeof(mapping_type) > 0);

int main() {}

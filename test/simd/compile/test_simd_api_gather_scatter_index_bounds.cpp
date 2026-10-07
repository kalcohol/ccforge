#include <simd>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's gather/scatter index bounds."
#endif

static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::int64_t) == 8 && sizeof(std::uint64_t) == 8);

#ifdef FORGE_SIMD_REQUIRE_ILP32_INDEX_BOUNDS
static_assert(sizeof(void*) == 4 && sizeof(std::ptrdiff_t) == 4);
static_assert(sizeof(std::simd::simd_size_type) == 4);
#endif

namespace {

using vector4 = std::simd::vec<int, 4>;
using size_type = std::simd::simd_size_type;
constexpr std::uint64_t high_unsigned = std::uint64_t{1} << 32;
constexpr std::int64_t high_signed = std::int64_t{1} << 32;

#ifndef FORGE_SIMD_INDEX_BOUNDS_PUBLIC_ONLY
using std::simd::detail::is_gather_scatter_index_in_range;

// These are auxiliary 32-bit-extent controls, not an ILP32 public-API run.
// Both original wide values alias small valid offsets if narrowed first.
static_assert(static_cast<std::int32_t>(high_unsigned + 1) == 1);
static_assert(static_cast<std::int32_t>(-high_signed + 2) == 2);
static_assert(!is_gather_scatter_index_in_range(high_unsigned + 1, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(high_unsigned + 2, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(high_signed + 1, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(-high_signed + 2, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::numeric_limits<std::uint64_t>::max(), std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::numeric_limits<std::int64_t>::min(), std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::numeric_limits<std::int64_t>::max(), std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::int64_t{-1}, std::int32_t{4}));
static_assert(is_gather_scatter_index_in_range(std::uint64_t{0}, std::int32_t{4}));
static_assert(is_gather_scatter_index_in_range(std::int64_t{3}, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::uint64_t{4}, std::int32_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::uint64_t{0}, std::int32_t{0}));
static_assert(!is_gather_scatter_index_in_range(std::uint64_t{0}, std::int32_t{-1}));

constexpr std::int32_t max_extent = std::numeric_limits<std::int32_t>::max();
static_assert(is_gather_scatter_index_in_range(std::int64_t{max_extent} - 1, max_extent));
static_assert(!is_gather_scatter_index_in_range(std::uint64_t{max_extent}, max_extent));
static_assert(!is_gather_scatter_index_in_range(std::uint64_t{max_extent} + 1, max_extent));
static_assert(is_gather_scatter_index_in_range(std::uint8_t{255}, std::int64_t{256}));
static_assert(is_gather_scatter_index_in_range(std::int32_t{3}, std::uint64_t{4}));
static_assert(!is_gather_scatter_index_in_range(std::int32_t{-1}, std::uint64_t{4}));
static_assert(is_gather_scatter_index_in_range(char8_t{3}, std::int32_t{4}));
static_assert(is_gather_scatter_index_in_range(char16_t{3}, std::int32_t{4}));
static_assert(is_gather_scatter_index_in_range(char32_t{3}, std::int32_t{4}));
static_assert(is_gather_scatter_index_in_range(wchar_t{3}, std::int32_t{4}));
#endif

template<class Index>
constexpr bool public_paths_preserve_wide_bounds(const std::array<Index, 4>& lanes) {
    using indices4 = std::simd::vec<Index, 4>;
    const indices4 indices([&](auto lane) { return lanes[decltype(lane)::value]; });
    const typename indices4::mask_type selected(0b1101u);
    const std::array<int, 4> input{11, 22, 33, 44};
    const vector4 values([](auto lane) { return 10 * (1 + static_cast<int>(decltype(lane)::value)); });
    const auto pointer_result = std::simd::partial_gather_from<vector4>(input.data(), 4, indices);
    const auto masked_pointer = std::simd::partial_gather_from<vector4>(
        input.data(), 4, selected, indices);
    const auto range_result = std::simd::partial_gather_from(input, indices);
    const auto masked_range = std::simd::partial_gather_from(input, selected, indices);
    for (size_type lane = 0; lane < 4; ++lane) {
        const int expected = lane == 0 ? 11 : lane == 3 ? 44 : 0;
        if (pointer_result[lane] != expected || masked_pointer[lane] != expected ||
            range_result[lane] != expected || masked_range[lane] != expected) {
            return false;
        }
    }

    std::array<int, 4> pointer_output{-1, -1, -1, -1};
    std::array<int, 4> masked_pointer_output{-1, -1, -1, -1};
    std::array<int, 4> range_output{-1, -1, -1, -1};
    std::array<int, 4> masked_range_output{-1, -1, -1, -1};
    std::simd::partial_scatter_to(values, pointer_output.data(), 4, indices);
    std::simd::partial_scatter_to(values, masked_pointer_output.data(), 4, selected, indices);
    std::simd::partial_scatter_to(values, range_output, indices);
    std::simd::partial_scatter_to(values, masked_range_output, selected, indices);
    const std::array<int, 4> expected_output{10, -1, -1, 40};
    return pointer_output == expected_output && masked_pointer_output == expected_output &&
        range_output == expected_output && masked_range_output == expected_output;
}

// On ILP32 these public controls distinguish the original narrowing bug without
// any out-of-bounds access: the old narrowed offsets would be 1 and 2.
static_assert(public_paths_preserve_wide_bounds(std::array<std::uint64_t, 4>{
    0, high_unsigned + 1, high_unsigned + 2, 3}));
static_assert(public_paths_preserve_wide_bounds(std::array<std::int64_t, 4>{
    0, high_signed + 1, -high_signed + 2, 3}));

} // namespace

int main() {}

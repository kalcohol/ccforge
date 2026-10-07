#include <simd>

#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <type_traits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's constrained overaligned flag."
#endif

namespace {

// [simd.syn] constrains the variable template, not just its tag's class body.
// https://eel.is/c++draft/simd.syn
template<std::size_t N>
concept has_overaligned_flag = requires { std::simd::flag_overaligned<N>; };

static_assert(!has_overaligned_flag<0>);
static_assert(!has_overaligned_flag<3>);
static_assert(!has_overaligned_flag<6>);
static_assert(!has_overaligned_flag<std::numeric_limits<std::size_t>::max()>);
static_assert(has_overaligned_flag<1>);
static_assert(has_overaligned_flag<2>);
static_assert(has_overaligned_flag<4>);
static_assert(has_overaligned_flag<64>);

constexpr std::size_t highest_power =
    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
static_assert(std::has_single_bit(highest_power));
static_assert(has_overaligned_flag<highest_power>);
static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::simd::flag_overaligned<64>)>,
    std::simd::flags<std::simd::overaligned_flag<64>>>);

constexpr bool valid_flags_keep_pointer_round_trips() {
    using vector4 = std::simd::vec<int, 4>;
    alignas(64) const std::array<int, 4> input{11, 22, 33, 44};
    alignas(64) std::array<int, 4> output{};
    constexpr auto flags = std::simd::flag_overaligned<64> |
        std::simd::flag_aligned | std::simd::flag_convert;
    const auto value = std::simd::partial_load<vector4>(input.data(), 4, flags);
    std::simd::partial_store(value, output.data(), 4, flags);
    return output == input;
}

static_assert(valid_flags_keep_pointer_round_trips());

} // namespace

int main() {}

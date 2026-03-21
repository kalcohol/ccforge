#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

constexpr bool constexpr_mask_from_bitset() {
    constexpr std::bitset<4> bits(0b0101u);
    constexpr mask4 values(bits);
    return values[0] && !values[1] && values[2] && !values[3];
}

constexpr bool constexpr_mask_bitset_roundtrip() {
    constexpr std::bitset<4> bits(0b0101u);
    constexpr mask4 values(bits);
    constexpr auto roundtrip = values.to_bitset();
    return roundtrip[0] && !roundtrip[1] && roundtrip[2] && !roundtrip[3] &&
        values.to_ullong() == 0b0101u;
}

static_assert(constexpr_mask_from_bitset(),
    "basic_mask(bitset) should remain constexpr-capable");
static_assert(constexpr_mask_bitset_roundtrip(),
    "basic_mask and bitset should support constexpr round-trip conversion");
static_assert(std::is_same<decltype(std::declval<const mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "const mask operator[] should return bool by value");
static_assert(std::is_same<decltype(std::declval<mask4&>()[std::simd::simd_size_type{}]), bool>::value,
    "non-const mask operator[] should also return bool by value");
static_assert(!std::is_assignable<decltype((std::declval<mask4&>()[std::simd::simd_size_type{}])), bool>::value,
    "mask operator[] should not expose writable lane references");
static_assert(std::is_same<decltype(std::declval<mask4&>() & std::declval<const mask4&>()), mask4>::value,
    "mask bitwise and should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() | std::declval<const mask4&>()), mask4>::value,
    "mask bitwise or should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<mask4&>() ^ std::declval<const mask4&>()), mask4>::value,
    "mask bitwise xor should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>().to_bitset()), std::bitset<4>>::value,
    "basic_mask::to_bitset should expose a matching std::bitset");
static_assert(std::is_same<decltype(std::declval<const mask4&>().to_ullong()), unsigned long long>::value,
    "basic_mask::to_ullong should expose an unsigned long long bit pattern");
static_assert(std::is_same<decltype(static_cast<int4>(std::declval<const mask4&>())), int4>::value,
    "basic_mask should provide same-width vector conversion");
static_assert(std::is_convertible<mask4, int4>::value,
    "basic_mask to same-byte basic_vec conversion should be implicit");
static_assert(std::is_constructible<longlong4, mask4>::value,
    "basic_mask should still support explicit different-byte vector conversion");
static_assert(!std::is_convertible<mask4, longlong4>::value,
    "basic_mask to different-byte basic_vec conversion should remain explicit");
static_assert(std::is_same<decltype(+std::declval<const mask4&>()), int4>::value,
    "mask unary plus should expose the signed integer lane vector");
static_assert(std::is_same<decltype(-std::declval<const mask4&>()), int4>::value,
    "mask unary minus should expose the signed integer lane vector");
static_assert(std::is_same<decltype(~std::declval<const mask4&>()), int4>::value,
    "mask unary bitwise not should expose the signed integer lane vector");
static_assert(std::is_same<decltype(std::declval<const mask4&>() == std::declval<const mask4&>()), mask4>::value,
    "mask equality should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() != std::declval<const mask4&>()), mask4>::value,
    "mask inequality should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() < std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() <= std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() > std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");
static_assert(std::is_same<decltype(std::declval<const mask4&>() >= std::declval<const mask4&>()), mask4>::value,
    "mask ordering comparison should remain on the mask type");

} // namespace

int main() {
    return 0;
}

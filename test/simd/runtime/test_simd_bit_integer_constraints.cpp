#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

template<class V>
constexpr int unsigned_bit_forms =
    int(requires(const V& v) { std::simd::bit_reverse(v); }) +
    int(requires(const V& v) { std::simd::popcount(v); }) +
    int(requires(const V& v) { std::simd::countl_zero(v); }) +
    int(requires(const V& v) { std::simd::countl_one(v); }) +
    int(requires(const V& v) { std::simd::countr_zero(v); }) +
    int(requires(const V& v) { std::simd::countr_one(v); }) +
    int(requires(const V& v) { std::simd::bit_width(v); }) +
    int(requires(const V& v) { std::simd::has_single_bit(v); }) +
    int(requires(const V& v) { std::simd::bit_floor(v); }) +
    int(requires(const V& v) { std::simd::bit_ceil(v); }) +
    int(requires(const V& v) { std::simd::rotl(v, 1); }) +
    int(requires(const V& v) { std::simd::rotr(v, 1); }) +
    int(requires(const V& v) { std::simd::rotl(v, v); }) +
    int(requires(const V& v) { std::simd::rotr(v, v); }) +
    int(requires(const V& v) { std::simd::bit_repeat(v, 1); }) +
    int(requires(const V& v) { std::simd::bit_repeat(v, v); }) +
    int(requires(const V& v) { std::simd::bit_compress(v, v); }) +
    int(requires(const V& v) { std::simd::bit_compress(v, typename V::value_type{}); }) +
    int(requires(const V& v) { std::simd::bit_expand(v, v); }) +
    int(requires(const V& v) { std::simd::bit_expand(v, typename V::value_type{}); });

template<class V, class S>
concept has_shl = requires(const V& value, const S& shift) { std::simd::shl(value, shift); };

template<class V, class S>
concept has_shr = requires(const V& value, const S& shift) { std::simd::shr(value, shift); };

template<class C>
constexpr bool character_positions_are_rejected() {
    using V = std::simd::vec<C, 4>;
    using U = std::simd::vec<std::make_unsigned_t<C>, 4>;
    return unsigned_bit_forms<V> == 0 &&
        !has_shl<V, int> && !has_shr<V, int> &&
        !has_shl<V, V> && !has_shr<V, V> &&
        !has_shl<U, C> && !has_shr<U, C> &&
        !has_shl<U, V> && !has_shr<U, V>;
}

static_assert(character_positions_are_rejected<char>());
static_assert(character_positions_are_rejected<wchar_t>());
static_assert(character_positions_are_rejected<char8_t>());
static_assert(character_positions_are_rejected<char16_t>());
static_assert(character_positions_are_rejected<char32_t>());
static_assert(unsigned_bit_forms<std::simd::vec<unsigned char, 4>> == 20);
static_assert(unsigned_bit_forms<std::simd::vec<unsigned short, 4>> == 20);
static_assert(unsigned_bit_forms<std::simd::vec<unsigned, 4>> == 20);
static_assert(unsigned_bit_forms<std::simd::vec<unsigned long, 4>> == 20);
static_assert(unsigned_bit_forms<std::simd::vec<unsigned long long, 4>> == 20);
static_assert(unsigned_bit_forms<simd_test::int4> == 0);
static_assert(has_shl<simd_test::int4, int>);
static_assert(has_shr<simd_test::int4, unsigned>);
static_assert(has_shl<simd_test::uint4, simd_test::int4>);
static_assert(has_shr<simd_test::int4, simd_test::uint4>);
static_assert(!has_shl<simd_test::uint4, bool>);
static_assert(!has_shr<simd_test::int4, bool>);
static_assert(!has_shl<simd_test::uint4, double>);
static_assert(!has_shr<simd_test::int4, simd_test::int2>);

using simd_test::uint4;
static_assert(std::same_as<decltype(std::simd::popcount(std::declval<const uint4&>())), simd_test::int4>);
static_assert(std::same_as<decltype(std::simd::has_single_bit(std::declval<const uint4&>())), uint4::mask_type>);
static_assert(noexcept(std::simd::bit_reverse(std::declval<const uint4&>())));
static_assert(noexcept(std::simd::popcount(std::declval<const uint4&>())));
static_assert(noexcept(std::simd::rotl(std::declval<const uint4&>(), 1)));
static_assert(!noexcept(std::simd::bit_repeat(std::declval<const uint4&>(), 2)));
static_assert(!noexcept(std::simd::bit_ceil(std::declval<const uint4&>())));

template<class C>
constexpr bool legal_character_controls() {
    using U = std::make_unsigned_t<C>;
    using V = std::simd::vec<U, 4>;
    using CV = std::simd::vec<C, 4>;
    // byteswap accepts integral elements; rotate/repeat vector counts are also integral.
    const CV characters(C{1});
    const CV lengths(C{2});
    const V value(U{1});
    const auto swapped = std::simd::byteswap(characters);
    const auto left = std::simd::rotl(value, characters);
    const auto right = std::simd::rotr(value, characters);
    const auto repeated = std::simd::bit_repeat(value, lengths);
    const auto repeated_scalar = std::simd::bit_repeat(value, 2);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        if (swapped[i] != std::byteswap(C{1}) ||
            left[i] != std::rotl(U{1}, 1) || right[i] != std::rotr(U{1}, 1) ||
            repeated[i] != repeated_scalar[i]) {
            return false;
        }
    }
    return true;
}

static_assert(legal_character_controls<char>());
static_assert(legal_character_controls<wchar_t>());
static_assert(legal_character_controls<char8_t>());
static_assert(legal_character_controls<char16_t>());
static_assert(legal_character_controls<char32_t>());

constexpr bool ordinary_bit_results() {
    const uint4 value(3u);
    const uint4 lengths(2u);
    const uint4 mask(10u);
    const auto reversed = std::simd::bit_reverse(value);
    const auto counted = std::simd::popcount(value);
    const auto leading_zero = std::simd::countl_zero(value);
    const auto leading_one = std::simd::countl_one(value);
    const auto trailing_zero = std::simd::countr_zero(value);
    const auto trailing_one = std::simd::countr_one(value);
    const auto width = std::simd::bit_width(value);
    const auto single = std::simd::has_single_bit(value);
    const auto floor = std::simd::bit_floor(value);
    const auto ceil = std::simd::bit_ceil(value);
    const auto repeated = std::simd::bit_repeat(value, lengths);
    const auto compressed = std::simd::bit_compress(value, mask);
    const auto expanded = std::simd::bit_expand(value, 10u);
    constexpr int digits = std::numeric_limits<unsigned>::digits;
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        if (reversed[i] != ((1u << (digits - 1)) | (1u << (digits - 2))) ||
            counted[i] != 2 || leading_zero[i] != digits - 2 || leading_one[i] != 0 ||
            trailing_zero[i] != 0 || trailing_one[i] != 2 || width[i] != 2 || single[i] ||
            floor[i] != 2u || ceil[i] != 4u || repeated[i] != std::numeric_limits<unsigned>::max() ||
            compressed[i] != 1u || expanded[i] != 10u) {
            return false;
        }
    }
    return true;
}

static_assert(ordinary_bit_results());

TEST(SimdBitIntegerConstraints, OrdinaryUnsignedAlgorithmsKeepTheirResults) {
    EXPECT_TRUE(ordinary_bit_results());
    const simd_test::uchar4 value(static_cast<unsigned char>(3));
    const auto result = std::simd::popcount(std::move(value));
    static_assert(std::same_as<std::remove_cv_t<decltype(result)>, simd_test::schar4>);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        EXPECT_EQ(result[i], 2);
    }
}

TEST(SimdBitIntegerConstraints, ByteswapAndIntegralVectorCountsStillAcceptCharacters) {
    EXPECT_TRUE(legal_character_controls<char>());
    EXPECT_TRUE(legal_character_controls<wchar_t>());
    EXPECT_TRUE(legal_character_controls<char8_t>());
    EXPECT_TRUE(legal_character_controls<char16_t>());
    EXPECT_TRUE(legal_character_controls<char32_t>());
}

TEST(SimdBitIntegerConstraints, SignedAndUnsignedIntegerShiftsRemainAvailable) {
    const simd_test::int4 value(-8);
    const simd_test::uint4 shift(1u);
    const auto left = std::simd::shl(value, shift);
    const auto right = std::simd::shr(value, shift);
    const auto reversed_left = std::simd::shl(value, -1);
    const auto reversed_right = std::simd::shr(value, -1);
    for (std::simd::simd_size_type i = 0; i < 4; ++i) {
        EXPECT_EQ(left[i], -16);
        EXPECT_EQ(right[i], -4);
        EXPECT_EQ(reversed_left[i], -4);
        EXPECT_EQ(reversed_right[i], -16);
    }
}

} // namespace

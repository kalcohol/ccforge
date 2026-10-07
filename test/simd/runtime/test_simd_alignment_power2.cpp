#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's alignment policy."
#endif

namespace {

using size_type = std::simd::simd_size_type;

template<class T, class U, std::size_t N>
constexpr bool has_rounded_alignment() {
    using vector = std::simd::basic_vec<T, std::simd::fixed_size_abi<N>>;
    using trait = std::simd::alignment<vector, U>;
    constexpr auto expected = std::bit_ceil(alignof(U) * N);
    return trait::value == expected && std::has_single_bit(trait::value) &&
        trait::value >= alignof(U) &&
        std::is_base_of_v<std::integral_constant<std::size_t, expected>, trait>;
}

template<class T, class U, std::size_t... I>
constexpr bool all_widths_have_rounded_alignment(std::index_sequence<I...>) {
    return (has_rounded_alignment<T, U, I + 1>() && ...);
}

using widths = std::make_index_sequence<64>;
static_assert(all_widths_have_rounded_alignment<float, float>(widths{}));
static_assert(all_widths_have_rounded_alignment<double, double>(widths{}));
static_assert(all_widths_have_rounded_alignment<int, unsigned char>(widths{}));
static_assert(all_widths_have_rounded_alignment<float, double>(widths{}));
static_assert(all_widths_have_rounded_alignment<
    std::complex<float>, std::complex<double>>(widths{}));

// The public odd-width aliases expose the original non-power-of-two result.
static_assert(std::has_single_bit(std::simd::alignment_v<std::simd::vec<float, 3>>));
static_assert(std::simd::alignment_v<std::simd::vec<float, 3>> ==
    std::bit_ceil(alignof(float) * std::size_t{3}));
static_assert(std::simd::alignment_v<std::simd::vec<double, 5>, float> ==
    std::bit_ceil(alignof(float) * std::size_t{5}));

// Existing power-of-two widths, including the native width, keep their values.
static_assert(std::simd::alignment_v<std::simd::vec<float, 1>> == alignof(float));
static_assert(std::simd::alignment_v<std::simd::vec<float, 4>> == alignof(float) * 4);
static_assert(std::simd::alignment_v<std::simd::vec<double, 8>> == alignof(double) * 8);
static_assert(std::simd::alignment_v<std::simd::vec<float>> ==
    alignof(float) * static_cast<std::size_t>(std::simd::vec<float>::size));

// Disabled vectors still provide valid metadata without rounding huge widths.
using disabled_value = std::simd::basic_vec<long double, std::simd::fixed_size_abi<4>>;
using disabled_width = std::simd::basic_vec<int, std::simd::fixed_size_abi<65>>;
using disabled_huge_width = std::simd::basic_vec<int,
    std::simd::fixed_size_abi<std::numeric_limits<size_type>::max()>>;
static_assert(std::simd::alignment_v<disabled_value, float> == alignof(float));
static_assert(std::simd::alignment_v<disabled_width, int> == alignof(int));
static_assert(std::simd::alignment_v<disabled_huge_width, double> == alignof(double));

template<class V, class U = typename V::value_type>
void check_aligned_round_trip() {
    constexpr size_type count = V::size;
    constexpr auto lanes = static_cast<std::size_t>(count);
    constexpr auto alignment = std::simd::alignment_v<V, U>;
    alignas(alignment) std::array<U, lanes + 1> input{};
    alignas(alignment) std::array<U, lanes + 1> output{};
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        input[lane] = static_cast<U>(lane + 1);
    }
    const U canary = static_cast<U>(77);
    input[lanes] = canary;
    output[lanes] = canary;

    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(input.data()) % alignment, 0u);
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(output.data()) % alignment, 0u);

    constexpr auto flags = std::simd::flag_aligned | std::simd::flag_convert;
    const V loaded = std::simd::partial_load<V>(input.data(), count, flags);
    std::simd::partial_store(loaded, output.data(), count, flags);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        EXPECT_EQ(loaded[static_cast<size_type>(lane)],
            static_cast<typename V::value_type>(input[lane]));
        EXPECT_EQ(output[lane], input[lane]);
    }
    EXPECT_EQ(input[lanes], canary);
    EXPECT_EQ(output[lanes], canary);
}

TEST(SimdAlignmentPower2, OddWidthsUseAlignedStorage) {
    check_aligned_round_trip<std::simd::vec<float, 3>>();
    check_aligned_round_trip<std::simd::vec<double, 5>>();
    check_aligned_round_trip<std::simd::vec<int, 7>>();
}

TEST(SimdAlignmentPower2, ConvertingLoadsAndStoresUseElementAlignment) {
    check_aligned_round_trip<std::simd::vec<float, 3>, double>();
    check_aligned_round_trip<std::simd::vec<double, 5>, short>();
    check_aligned_round_trip<std::simd::vec<int, 7>, unsigned char>();
}

TEST(SimdAlignmentPower2, ComplexOddWidthsUseAlignedStorage) {
    check_aligned_round_trip<std::simd::vec<std::complex<float>, 3>>();
    check_aligned_round_trip<std::simd::vec<std::complex<double>, 5>>();
}

TEST(SimdAlignmentPower2, PowerOfTwoWidthsKeepAlignedRoundTrips) {
    check_aligned_round_trip<std::simd::vec<float, 4>>();
    check_aligned_round_trip<std::simd::vec<double, 8>>();
    check_aligned_round_trip<std::simd::vec<float>>();
}

} // namespace

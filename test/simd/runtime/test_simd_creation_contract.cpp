#include <simd>

#include <gtest/gtest.h>

#include <complex>
#include <tuple>

namespace {

using int2 = std::simd::vec<int, 2>;
using int4 = std::simd::vec<int, 4>;
using int5 = std::simd::vec<int, 5>;
using int8 = std::simd::vec<int, 8>;
using mask2 = typename int2::mask_type;
using mask4 = typename int4::mask_type;
using mask5 = typename int5::mask_type;

template<class V>
constexpr V sequence(int first) {
    return V([first](auto i) { return first + static_cast<int>(i); });
}

template<class V>
constexpr bool is_sequence(const V& value, int first) {
    for (std::simd::simd_size_type i = 0; i < V::size; ++i) {
        if (value[i] != first + static_cast<int>(i)) {
            return false;
        }
    }
    return true;
}

constexpr bool constexpr_creation() {
    const auto even = std::simd::chunk<int2>(sequence<int4>(10));
    const auto uneven = std::simd::chunk<2>(sequence<int5>(20));
    const auto larger = std::simd::chunk<int8>(sequence<int4>(30));
    const auto joined = std::simd::cat(
        std::get<0>(uneven), std::get<1>(uneven), std::get<2>(uneven));
    const mask5 mask([](auto i) { return i % 2 == 0; });
    const auto mask_parts = std::simd::chunk<mask2>(mask);
    const auto mask_joined = std::simd::cat(
        std::get<0>(mask_parts), std::get<1>(mask_parts), std::get<2>(mask_parts));
    for (std::simd::simd_size_type i = 0; i < mask5::size; ++i) {
        if (mask_joined[i] != mask[i]) {
            return false;
        }
    }
    return is_sequence(even[0], 10) && is_sequence(even[1], 12) &&
        is_sequence(joined, 20) && is_sequence(std::get<0>(larger), 30);
}

static_assert(constexpr_creation());

TEST(SimdCreationContract, EvenTypeAndWidthChunksPreserveLanes) {
    const auto input = sequence<int4>(10);
    const auto typed = std::simd::chunk<int2>(input);
    const auto numbered = std::simd::chunk<2>(input);
    EXPECT_TRUE(is_sequence(typed[0], 10));
    EXPECT_TRUE(is_sequence(typed[1], 12));
    EXPECT_TRUE(is_sequence(numbered[0], 10));
    EXPECT_TRUE(is_sequence(numbered[1], 12));
    EXPECT_TRUE(is_sequence(std::simd::cat(typed[0], typed[1]), 10));
}

TEST(SimdCreationContract, UnevenChunksKeepTheTailAndRoundTrip) {
    const auto input = sequence<int5>(20);
    const auto parts = std::simd::chunk<int2>(input);
    EXPECT_TRUE(is_sequence(std::get<0>(parts), 20));
    EXPECT_TRUE(is_sequence(std::get<1>(parts), 22));
    EXPECT_TRUE(is_sequence(std::get<2>(parts), 24));
    EXPECT_TRUE(is_sequence(std::simd::cat(
        std::get<0>(parts), std::get<1>(parts), std::get<2>(parts)), 20));
}

TEST(SimdCreationContract, LargerChunkProducesOneResizedTail) {
    const auto parts = std::simd::chunk<int8>(sequence<int4>(30));
    EXPECT_TRUE(is_sequence(std::get<0>(parts), 30));
    EXPECT_TRUE(is_sequence(std::simd::cat(std::get<0>(parts)), 30));
}

TEST(SimdCreationContract, CatAcceptsDifferentAbisWithTheSameElementType) {
    using native_int = std::simd::basic_vec<int>;
    const auto first = sequence<int2>(40);
    const auto second = sequence<native_int>(42);
    EXPECT_TRUE(is_sequence(std::simd::cat(first, second), 40));
    const auto reversed = std::simd::cat(second, first);
    for (std::simd::simd_size_type i = 0; i < native_int::size; ++i) {
        EXPECT_EQ(reversed[i], second[i]);
    }
    EXPECT_EQ(reversed[native_int::size], first[0]);
    EXPECT_EQ(reversed[native_int::size + 1], first[1]);
}

TEST(SimdCreationContract, EvenAndUnevenMaskChunksRetainBooleanLanes) {
    const mask4 even([](auto i) { return i % 2 == 0; });
    const auto even_parts = std::simd::chunk<2>(even);
    const auto even_joined = std::simd::cat(even_parts[0], even_parts[1]);
    for (std::simd::simd_size_type i = 0; i < mask4::size; ++i) {
        EXPECT_EQ(even_joined[i], even[i]);
    }
    const mask5 uneven([](auto i) { return i == 0 || i == 3 || i == 4; });
    const auto parts = std::simd::chunk<mask2>(uneven);
    const auto joined = std::simd::cat(
        std::get<0>(parts), std::get<1>(parts), std::get<2>(parts));
    for (std::simd::simd_size_type i = 0; i < mask5::size; ++i) {
        EXPECT_EQ(joined[i], uneven[i]);
    }
}

TEST(SimdCreationContract, MaskCatAcceptsDifferentAbisWithMatchingBytes) {
    using native_mask = typename std::simd::basic_vec<int>::mask_type;
    const mask2 first([](auto i) { return i == 0; });
    const native_mask second([](auto i) { return i % 2 != 0; });
    const auto joined = std::simd::cat(first, second);
    EXPECT_TRUE(joined[0]);
    EXPECT_FALSE(joined[1]);
    for (std::simd::simd_size_type i = 0; i < native_mask::size; ++i) {
        EXPECT_EQ(joined[i + 2], second[i]);
    }
}

TEST(SimdCreationContract, ComplexChunksAndCatDoNotConvertTheirElements) {
    using complex2 = std::simd::vec<std::complex<float>, 2>;
    using complex5 = std::simd::vec<std::complex<float>, 5>;
    const complex5 input([](auto i) {
        const float index = static_cast<float>(i);
        return std::complex<float>(index + 1.0f, -index - 10.0f);
    });
    const auto parts = std::simd::chunk<complex2>(input);
    const auto joined = std::simd::cat(
        std::get<0>(parts), std::get<1>(parts), std::get<2>(parts));
    for (std::simd::simd_size_type i = 0; i < complex5::size; ++i) {
        EXPECT_EQ(joined[i], input[i]);
    }
}

} // namespace

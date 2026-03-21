#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <limits>

namespace {

using namespace simd_test;

struct simd_plus {
    template<class T, class Abi>
    constexpr std::simd::basic_vec<T, Abi> operator()(const std::simd::basic_vec<T, Abi>& left,
                                                      const std::simd::basic_vec<T, Abi>& right) const noexcept {
        return left + right;
    }
};

TEST(SimdRuntimeTest, ReduceFamilyProducesExpectedValues) {
    const std::array<int, 4> data{{1, 4, 2, 3}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);
    const mask4 none_selected(0u);

    EXPECT_EQ(std::simd::reduce(values), 10);
    EXPECT_EQ(std::simd::reduce(values, selected), 3);
    EXPECT_EQ(std::simd::reduce_min(values), 1);
    EXPECT_EQ(std::simd::reduce_min(values, selected), 1);
    EXPECT_EQ(std::simd::reduce_min(values, none_selected), std::numeric_limits<int>::max());
    EXPECT_EQ(std::simd::reduce_max(values), 4);
    EXPECT_EQ(std::simd::reduce_max(values, selected), 2);
    EXPECT_EQ(std::simd::reduce_max(values, none_selected), std::numeric_limits<int>::lowest());
}

TEST(SimdRuntimeTest, ReduceAcceptsVecOneReducerModel) {
    const std::array<int, 4> data{{1, 2, 3, 4}};
    const int4 values = load_vec<int4>(data);

    EXPECT_EQ(std::simd::reduce(values, simd_plus{}), 10);
}

TEST(SimdRuntimeTest, MaskedBitwiseReduceUsesStandardDefaultIdentities) {
    const std::array<int, 4> data{{0b1111, 0b1100, 0b1010, 0b1001}};
    const int4 values = load_vec<int4>(data);
    const mask4 selected(0b0101u);

    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_and<>{}), 0b1010);
    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_or<>{}), 0b1111);
    EXPECT_EQ(std::simd::reduce(values, selected, std::bit_xor<>{}), 0b0101);
}

} // namespace

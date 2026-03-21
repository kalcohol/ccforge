#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <complex>

namespace {

using namespace simd_test;

TEST(SimdMemorySupportedTypeTest, DefaultComplexLoadAndStoreEntryPointsRoundTrip) {
    constexpr auto lane_count = static_cast<size_t>(default_complexf::size);
    std::array<std::complex<float>, lane_count> input{};
    std::array<std::complex<float>, lane_count> partial_output{};
    std::array<std::complex<float>, lane_count> unchecked_output{};

    for (size_t i = 0; i < lane_count; ++i) {
        input[i] = std::complex<float>(static_cast<float>(i + 1), static_cast<float>(-static_cast<int>(i) - 1));
    }

    const auto partial = std::simd::partial_load(input.data(), static_cast<std::simd::simd_size_type>(lane_count));
    const auto unchecked = std::simd::unchecked_load(input.data());

    static_assert(std::is_same_v<std::remove_cv_t<decltype(partial)>, default_complexf>);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(unchecked)>, default_complexf>);

    std::simd::partial_store(partial, partial_output.data(), static_cast<std::simd::simd_size_type>(lane_count));
    std::simd::unchecked_store(unchecked, unchecked_output.data());

    for (size_t i = 0; i < lane_count; ++i) {
        EXPECT_EQ(partial[i], input[i]);
        EXPECT_EQ(unchecked[i], input[i]);
        EXPECT_EQ(partial_output[i], input[i]);
        EXPECT_EQ(unchecked_output[i], input[i]);
    }
}

} // namespace

#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace simd_test;

int4 make_int4(int v0, int v1, int v2, int v3) {
    const std::array<int, 4> data{{v0, v1, v2, v3}};
    return std::simd::partial_load<int4>(data.data(), 4);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStorePreserveExactValues) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};

    const auto value = std::simd::partial_load<int4>(input.data(), 4);
    std::simd::partial_store(value, output.data(), 4);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryLoadStoreTest, PartialMaskedLoadAndStoreAffectOnlySelectedLanes) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{9, 9, 9, 9}};
    mask4 selected(0b1010u);

    const auto value = std::simd::partial_load<int4>(input.data(), 4, selected);
    std::simd::partial_store(value, output.data(), 4, selected);

    EXPECT_EQ(lane(value, 0), 0);
    EXPECT_EQ(lane(value, 1), 2);
    EXPECT_EQ(lane(value, 2), 0);
    EXPECT_EQ(lane(value, 3), 4);

    EXPECT_EQ(output[0], 9);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 9);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadZeroFillsTailLanes) {
    std::array<int, 2> input{{7, 9}};

    const auto value = std::simd::partial_load<int4>(input.data(), 2);

    EXPECT_EQ(lane(value, 0), 7);
    EXPECT_EQ(lane(value, 1), 9);
    EXPECT_EQ(lane(value, 2), 0);
    EXPECT_EQ(lane(value, 3), 0);
}

TEST(SimdMemoryLoadStoreTest, PartialStorePreservesElementsBeyondCount) {
    std::array<int, 4> output{{9, 9, 9, 9}};
    int4 value = make_int4(1, 2, 3, 4);

    std::simd::partial_store(value, output.data(), 2);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 9);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreTest, ZeroCountLoadAndStoreProduceNoObservableWrites) {
    std::array<int, 4> input{{7, 8, 9, 10}};
    std::array<int, 4> output{{5, 5, 5, 5}};

    const auto value = std::simd::partial_load<int4>(input.data(), 0);
    std::simd::partial_store(value, output.data(), 0);

    EXPECT_EQ(lane(value, 0), 0);
    EXPECT_EQ(lane(value, 1), 0);
    EXPECT_EQ(lane(value, 2), 0);
    EXPECT_EQ(lane(value, 3), 0);
    EXPECT_EQ(output[0], 5);
    EXPECT_EQ(output[1], 5);
    EXPECT_EQ(output[2], 5);
    EXPECT_EQ(output[3], 5);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStoreSupportIteratorSentinelOverloads) {
    std::vector<int> input{3, 5, 7, 11};
    std::vector<int> output{9, 9, 9, 9};

    const auto value = std::simd::partial_load<int4>(input.cbegin(), input.cbegin() + 3);
    std::simd::partial_store(value, output.begin(), output.begin() + 3);

    EXPECT_EQ(lane(value, 0), 3);
    EXPECT_EQ(lane(value, 1), 5);
    EXPECT_EQ(lane(value, 2), 7);
    EXPECT_EQ(lane(value, 3), 0);

    EXPECT_EQ(output[0], 3);
    EXPECT_EQ(output[1], 5);
    EXPECT_EQ(output[2], 7);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStoreSupportIteratorCountOverloads) {
    std::vector<int> input{3, 5, 7, 11};
    std::vector<int> output{9, 9, 9, 9};

    const auto value = std::simd::partial_load<int4>(input.cbegin(), 3);
    std::simd::partial_store(value, output.begin(), 3);

    EXPECT_EQ(lane(value, 0), 3);
    EXPECT_EQ(lane(value, 1), 5);
    EXPECT_EQ(lane(value, 2), 7);
    EXPECT_EQ(lane(value, 3), 0);

    EXPECT_EQ(output[0], 3);
    EXPECT_EQ(output[1], 5);
    EXPECT_EQ(output[2], 7);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStoreSupportContiguousRangeOverloads) {
    std::array<int, 4> input{{3, 5, 7, 11}};
    std::array<int, 4> output{{9, 9, 9, 9}};
    const std::span<const int, 4> input_view(input);
    std::span<int, 4> output_view(output);

    const auto value = std::simd::partial_load<int4>(input_view);
    std::simd::partial_store(value, output_view);

    EXPECT_EQ(lane(value, 0), 3);
    EXPECT_EQ(lane(value, 1), 5);
    EXPECT_EQ(lane(value, 2), 7);
    EXPECT_EQ(lane(value, 3), 11);
    EXPECT_EQ(output[0], 3);
    EXPECT_EQ(output[3], 11);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStoreSupportMaskedIteratorSentinelOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected(0b1101u);

    const auto value = std::simd::partial_load<int4>(input.cbegin(), input.cbegin() + 3, selected);
    std::simd::partial_store(value, output.begin(), output.begin() + 3, selected);

    EXPECT_EQ(lane(value, 0), 2);
    EXPECT_EQ(lane(value, 1), 0);
    EXPECT_EQ(lane(value, 2), 6);
    EXPECT_EQ(lane(value, 3), 0);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 9);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreTest, PartialLoadAndStoreSupportMaskedIteratorCountOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected(0b1101u);

    const auto value = std::simd::partial_load<int4>(input.cbegin(), 3, selected);
    std::simd::partial_store(value, output.begin(), 3, selected);

    EXPECT_EQ(lane(value, 0), 2);
    EXPECT_EQ(lane(value, 1), 0);
    EXPECT_EQ(lane(value, 2), 6);
    EXPECT_EQ(lane(value, 3), 0);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 9);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreTest, ConvertFlagEnablesTypeChangingPartialStore) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<float, 4> output{{0.0f, 0.0f, 0.0f, 0.0f}};

    const auto value = std::simd::partial_load<int4>(input.data(), 4);
    std::simd::partial_store(value, output.data(), 4, std::simd::flag_convert);

    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 2.0f);
    EXPECT_FLOAT_EQ(output[2], 3.0f);
    EXPECT_FLOAT_EQ(output[3], 4.0f);
}

TEST(SimdMemoryLoadStoreTest, ValuePreservingConversionsDoNotRequireConvertFlag) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<long long, 4> output{{0, 0, 0, 0}};

    const auto widened = std::simd::partial_load<longlong4>(input.data(), 4);
    std::simd::partial_store(make_int4(1, 2, 3, 4), output.data(), 4);

    EXPECT_EQ(widened[0], 1);
    EXPECT_EQ(widened[1], 2);
    EXPECT_EQ(widened[2], 3);
    EXPECT_EQ(widened[3], 4);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryLoadStoreTest, AlignmentFlagsAreAcceptedByPartialLoad) {
    alignas(std::simd::alignment_v<int4>) int input[4]{5, 6, 7, 8};
    const auto input_address = reinterpret_cast<std::uintptr_t>(input);
    ASSERT_EQ(input_address % std::simd::alignment_v<int4>, 0u)
        << "alignment flag tests should use genuinely aligned storage";

    const auto aligned = std::simd::partial_load<int4>(input, 4, std::simd::flag_aligned);
    const auto overaligned = std::simd::partial_load<int4>(input, 4, std::simd::flag_overaligned<16>);

    EXPECT_EQ(lane(aligned, 0), 5);
    EXPECT_EQ(lane(aligned, 3), 8);
    EXPECT_EQ(lane(overaligned, 0), 5);
    EXPECT_EQ(lane(overaligned, 3), 8);
}

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)

TEST(SimdMemoryLoadStoreExtensionTest, UncheckedLoadAndStorePreserveExactValues) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};

    const auto value = std::simd::unchecked_load<int4>(input.data());
    std::simd::unchecked_store(value, output.data());

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryLoadStoreExtensionTest, UncheckedLoadAndStoreSupportIteratorSentinelOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{0, 0, 0, 0};

    const auto value = std::simd::unchecked_load<int4>(input.cbegin(), input.cend(), std::simd::flag_default);
    std::simd::unchecked_store(value, output.begin(), output.end(), std::simd::flag_default);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 4);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 8);
}

TEST(SimdMemoryLoadStoreExtensionTest, UncheckedLoadAndStoreSupportIteratorCountOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{0, 0, 0, 0};

    const auto value = std::simd::unchecked_load<int4>(input.cbegin(), 4);
    std::simd::unchecked_store(value, output.begin(), 4);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 4);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 8);
}

TEST(SimdMemoryLoadStoreExtensionTest, UncheckedMaskedIteratorCountOverloadsHonorMask) {
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected(0b0101u);

    const auto value = std::simd::unchecked_load<int4>(input.cbegin(), 4, selected);
    std::simd::unchecked_store(value, output.begin(), 4, selected);

    EXPECT_EQ(lane(value, 0), 1);
    EXPECT_EQ(lane(value, 1), 0);
    EXPECT_EQ(lane(value, 2), 3);
    EXPECT_EQ(lane(value, 3), 0);
    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 9);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryLoadStoreExtensionTest, UncheckedMaskedPointerOverloadsHonorMask) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{9, 9, 9, 9}};
    mask4 selected(0b0101u);

    const auto value = std::simd::unchecked_load<int4>(input.data(), selected);
    std::simd::unchecked_store(value, output.data(), selected);

    EXPECT_EQ(lane(value, 0), 1);
    EXPECT_EQ(lane(value, 1), 0);
    EXPECT_EQ(lane(value, 2), 3);
    EXPECT_EQ(lane(value, 3), 0);
    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 9);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 9);
}

#else

TEST(SimdMemoryLoadStoreDraftTest, UncheckedMemoryCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS after unchecked_* stabilizes.";
}

#endif

} // namespace

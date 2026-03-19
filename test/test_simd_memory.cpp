#include <simd>

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace {

using int4 = std::simd::vec<int, 4>;
using longlong4 = std::simd::vec<long long, 4>;
using mask4 = std::simd::mask<int, 4>;

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

TEST(SimdMemoryTest, PartialLoadAndStorePreserveExactValues) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};

    const auto value = std::simd::partial_load<int4>(input.data(), 4);
    std::simd::partial_store(value, output.data(), 4);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryTest, PartialMaskedLoadAndStoreAffectOnlySelectedLanes) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{9, 9, 9, 9}};
    mask4 selected{false, true, false, true};

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

TEST(SimdMemoryTest, PartialLoadZeroFillsTailLanes) {
    std::array<int, 2> input{{7, 9}};

    const auto value = std::simd::partial_load<int4>(input.data(), 2);

    EXPECT_EQ(lane(value, 0), 7);
    EXPECT_EQ(lane(value, 1), 9);
    EXPECT_EQ(lane(value, 2), 0);
    EXPECT_EQ(lane(value, 3), 0);
}

TEST(SimdMemoryTest, PartialStorePreservesElementsBeyondCount) {
    std::array<int, 4> output{{9, 9, 9, 9}};
    int4 value{1, 2, 3, 4};

    std::simd::partial_store(value, output.data(), 2);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 9);
    EXPECT_EQ(output[3], 9);
}

TEST(SimdMemoryTest, ZeroCountLoadAndStoreProduceNoObservableWrites) {
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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportIteratorSentinelOverloads) {
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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportIteratorCountOverloads) {
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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportMaskedIteratorSentinelOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected{true, false, true, true};

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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportMaskedIteratorCountOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected{true, false, true, true};

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

TEST(SimdMemoryTest, ConvertFlagEnablesTypeChangingPartialStore) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<float, 4> output{{0.0f, 0.0f, 0.0f, 0.0f}};

    const auto value = std::simd::partial_load<int4>(input.data(), 4);
    std::simd::partial_store(value, output.data(), 4, std::simd::flag_convert);

    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 2.0f);
    EXPECT_FLOAT_EQ(output[2], 3.0f);
    EXPECT_FLOAT_EQ(output[3], 4.0f);
}

TEST(SimdMemoryTest, ValuePreservingConversionsDoNotRequireConvertFlag) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<long long, 4> output{{0, 0, 0, 0}};

    const auto widened = std::simd::partial_load<longlong4>(input.data(), 4);
    std::simd::partial_store(int4{1, 2, 3, 4}, output.data(), 4);

    EXPECT_EQ(widened[0], 1);
    EXPECT_EQ(widened[1], 2);
    EXPECT_EQ(widened[2], 3);
    EXPECT_EQ(widened[3], 4);

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryTest, AlignmentFlagsAreAcceptedByPartialLoad) {
    std::array<int, 4> input{{5, 6, 7, 8}};

    const auto aligned = std::simd::partial_load<int4>(input.data(), 4, std::simd::flag_aligned);
    const auto overaligned = std::simd::partial_load<int4>(input.data(), 4, std::simd::flag_overaligned<16>);

    EXPECT_EQ(lane(aligned, 0), 5);
    EXPECT_EQ(lane(aligned, 3), 8);
    EXPECT_EQ(lane(overaligned, 0), 5);
    EXPECT_EQ(lane(overaligned, 3), 8);
}

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)

TEST(SimdMemoryExtensionTest, UncheckedLoadAndStorePreserveExactValues) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{0, 0, 0, 0}};

    const auto value = std::simd::unchecked_load<int4>(input.data());
    std::simd::unchecked_store(value, output.data());

    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 2);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(output[3], 4);
}

TEST(SimdMemoryExtensionTest, UncheckedLoadAndStoreSupportIteratorSentinelOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{0, 0, 0, 0};

    const auto value = std::simd::unchecked_load<int4>(input.cbegin(), input.cend(), std::simd::flag_default);
    std::simd::unchecked_store(value, output.begin(), output.end(), std::simd::flag_default);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 4);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 8);
}

TEST(SimdMemoryExtensionTest, UncheckedLoadAndStoreSupportIteratorCountOverloads) {
    std::vector<int> input{2, 4, 6, 8};
    std::vector<int> output{0, 0, 0, 0};

    const auto value = std::simd::unchecked_load<int4>(input.cbegin(), 4);
    std::simd::unchecked_store(value, output.begin(), 4);

    EXPECT_EQ(output[0], 2);
    EXPECT_EQ(output[1], 4);
    EXPECT_EQ(output[2], 6);
    EXPECT_EQ(output[3], 8);
}

TEST(SimdMemoryExtensionTest, UncheckedMaskedIteratorCountOverloadsHonorMask) {
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output{9, 9, 9, 9};
    mask4 selected{true, false, true, false};

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

TEST(SimdMemoryExtensionTest, UncheckedMaskedPointerOverloadsHonorMask) {
    std::array<int, 4> input{{1, 2, 3, 4}};
    std::array<int, 4> output{{9, 9, 9, 9}};
    mask4 selected{true, false, true, false};

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

TEST(SimdMemoryDraftTest, UncheckedMemoryCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS after unchecked_* stabilizes.";
}

#endif

} // namespace

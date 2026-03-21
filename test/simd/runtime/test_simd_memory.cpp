#include <simd>

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>
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

int4 make_int4(int v0, int v1, int v2, int v3) {
    const std::array<int, 4> data{{v0, v1, v2, v3}};
    return std::simd::partial_load<int4>(data.data(), 4);
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
    int4 value = make_int4(1, 2, 3, 4);

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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportContiguousRangeOverloads) {
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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportMaskedIteratorSentinelOverloads) {
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

TEST(SimdMemoryTest, PartialLoadAndStoreSupportMaskedIteratorCountOverloads) {
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

TEST(SimdMemoryTest, AlignmentFlagsAreAcceptedByPartialLoad) {
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

TEST(SimdMemoryTest, UncheckedGatherAndScatterRoundTrip) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(6, 0, 3, 7);

    const auto gathered = std::simd::unchecked_gather_from<int4>(input.data(), indices);
    EXPECT_EQ(gathered[0], 16);
    EXPECT_EQ(gathered[1], 10);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 17);

    std::simd::unchecked_scatter_to(gathered, output.data(), indices);
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output[3], 13);
    EXPECT_EQ(output[6], 16);
    EXPECT_EQ(output[7], 17);
    EXPECT_EQ(output[1], -1);
}

TEST(SimdMemoryTest, PartialGatherZeroFillsOffsetsOutsideCount) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    const int4 indices = make_int4(6, 0, 3, 7);

    const auto gathered = std::simd::partial_gather_from<int4>(input.data(), 6, indices);
    EXPECT_EQ(gathered[0], 0);
    EXPECT_EQ(gathered[1], 10);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);
}

TEST(SimdMemoryTest, PartialGatherHonorsMaskAndCount) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    const int4 indices = make_int4(6, 0, 3, 7);
    const mask4 selected(0b0101u);

    const auto gathered = std::simd::partial_gather_from<int4>(input.data(), 6, selected, indices);
    EXPECT_EQ(gathered[0], 0);
    EXPECT_EQ(gathered[1], 0);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);
}

TEST(SimdMemoryTest, PartialGatherAndScatterSupportContiguousRanges) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const std::span<const int, 8> input_view(input);
    std::span<int, 8> output_view(output);

    const auto gathered = std::simd::partial_gather_from(input_view, indices);
    static_assert(std::is_same<std::remove_const_t<decltype(gathered)>, int4>::value,
        "range gather should default to vec<range_value_t<R>, Indices::size()>");
    std::simd::partial_scatter_to(gathered, output_view, indices);

    EXPECT_EQ(gathered[0], 11);
    EXPECT_EQ(gathered[1], 14);
    EXPECT_EQ(gathered[2], 15);
    EXPECT_EQ(gathered[3], 17);
    EXPECT_EQ(output[1], 11);
    EXPECT_EQ(output[4], 14);
    EXPECT_EQ(output[5], 15);
    EXPECT_EQ(output[7], 17);
}

TEST(SimdMemoryTest, PartialScatterSkipsOffsetsOutsideCount) {
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const int4 values = make_int4(10, 20, 30, 40);

    std::simd::partial_scatter_to(values, output.data(), 6, indices);

    EXPECT_EQ(output[1], 10);
    EXPECT_EQ(output[4], 20);
    EXPECT_EQ(output[5], 30);
    EXPECT_EQ(output[7], -1);
}

TEST(SimdMemoryTest, PartialScatterHonorsMaskAndCount) {
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(1, 4, 5, 7);
    const int4 values = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::partial_scatter_to(values, output.data(), 6, selected, indices);

    EXPECT_EQ(output[1], 10);
    EXPECT_EQ(output[4], -1);
    EXPECT_EQ(output[5], 30);
    EXPECT_EQ(output[7], -1);
}

TEST(SimdMemoryTest, WhereExpressionAssignsAndCopiesOnlySelectedLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 other = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::where(selected, values) = other;
    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 30);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) = 7;
    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], 4);

    std::array<int, 4> from{{100, 200, 300, 400}};
    std::simd::where(selected, values).copy_from(from.data());
    EXPECT_EQ(values[0], 100);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 300);
    EXPECT_EQ(values[3], 4);

    std::array<int, 4> to{{-1, -1, -1, -1}};
    std::simd::where(selected, values).copy_to(to.data());
    EXPECT_EQ(to[0], 100);
    EXPECT_EQ(to[1], -1);
    EXPECT_EQ(to[2], 300);
    EXPECT_EQ(to[3], -1);

    const int4& const_values = values;
    std::array<int, 4> to_const{{-1, -1, -1, -1}};
    std::simd::where(selected, const_values).copy_to(to_const.data());
    EXPECT_EQ(to_const[0], 100);
    EXPECT_EQ(to_const[1], -1);
    EXPECT_EQ(to_const[2], 300);
    EXPECT_EQ(to_const[3], -1);
}

TEST(SimdMemoryTest, WhereExpressionCompoundAssignmentsModifyOnlySelectedLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 add = make_int4(10, 20, 30, 40);
    const mask4 selected(0b0101u);

    std::simd::where(selected, values) += add;
    EXPECT_EQ(values[0], 11);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 33);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) *= 2;
    EXPECT_EQ(values[0], 22);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 66);
    EXPECT_EQ(values[3], 4);

    std::simd::where(selected, values) -= make_int4(2, 2, 2, 2);
    EXPECT_EQ(values[0], 20);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 64);
    EXPECT_EQ(values[3], 4);

    {
        int4 div_values = make_int4(100, 10, 60, 40);
        const int4 divisors = make_int4(5, 2, 3, 2);

        std::simd::where(selected, div_values) /= divisors;
        EXPECT_EQ(div_values[0], 20);
        EXPECT_EQ(div_values[1], 10);
        EXPECT_EQ(div_values[2], 20);
        EXPECT_EQ(div_values[3], 40);
    }

    {
        int4 mod_values = make_int4(17, 18, 19, 20);
        const int4 moduli = make_int4(5, 7, 6, 4);

        std::simd::where(selected, mod_values) %= moduli;
        EXPECT_EQ(mod_values[0], 2);
        EXPECT_EQ(mod_values[1], 18);
        EXPECT_EQ(mod_values[2], 1);
        EXPECT_EQ(mod_values[3], 20);
    }

    {
        int4 and_values = make_int4(14, 1, 11, 5); // 0b1110, 0b0001, 0b1011, 0b0101
        const int4 and_rhs = make_int4(10, 0, 12, 0); // 0b1010, 0, 0b1100, 0

        std::simd::where(selected, and_values) &= and_rhs;
        EXPECT_EQ(and_values[0], 10);
        EXPECT_EQ(and_values[1], 1);
        EXPECT_EQ(and_values[2], 8);
        EXPECT_EQ(and_values[3], 5);
    }

    {
        int4 or_values = make_int4(8, 0, 2, 0); // 0b1000, 0, 0b0010, 0
        const int4 or_rhs = make_int4(3, 0, 8, 0); // 0b0011, 0, 0b1000, 0

        std::simd::where(selected, or_values) |= or_rhs;
        EXPECT_EQ(or_values[0], 11);
        EXPECT_EQ(or_values[1], 0);
        EXPECT_EQ(or_values[2], 10);
        EXPECT_EQ(or_values[3], 0);
    }

    {
        int4 xor_values = make_int4(10, 0, 12, 0); // 0b1010, 0, 0b1100, 0
        const int4 xor_rhs = make_int4(15, 0, 3, 0); // 0b1111, 0, 0b0011, 0

        std::simd::where(selected, xor_values) ^= xor_rhs;
        EXPECT_EQ(xor_values[0], 5);
        EXPECT_EQ(xor_values[1], 0);
        EXPECT_EQ(xor_values[2], 15);
        EXPECT_EQ(xor_values[3], 0);
    }

    {
        int4 shift_values = make_int4(1, 2, 3, 4);
        std::simd::where(selected, shift_values) <<= 2;
        EXPECT_EQ(shift_values[0], 4);
        EXPECT_EQ(shift_values[1], 2);
        EXPECT_EQ(shift_values[2], 12);
        EXPECT_EQ(shift_values[3], 4);

        std::simd::where(selected, shift_values) >>= 1;
        EXPECT_EQ(shift_values[0], 2);
        EXPECT_EQ(shift_values[2], 6);
    }

    {
        int4 mod_scalar = make_int4(17, 18, 19, 20);
        std::simd::where(selected, mod_scalar) %= 4;
        EXPECT_EQ(mod_scalar[0], 1);
        EXPECT_EQ(mod_scalar[2], 3);
    }

    {
        int4 and_scalar = make_int4(14, 1, 11, 5);
        std::simd::where(selected, and_scalar) &= 6;
        EXPECT_EQ(and_scalar[0], 6);
        EXPECT_EQ(and_scalar[2], 2);
    }

    {
        int4 or_scalar = make_int4(8, 0, 2, 0);
        std::simd::where(selected, or_scalar) |= 1;
        EXPECT_EQ(or_scalar[0], 9);
        EXPECT_EQ(or_scalar[2], 3);
    }

    {
        int4 xor_scalar = make_int4(10, 0, 12, 0);
        std::simd::where(selected, xor_scalar) ^= 3;
        EXPECT_EQ(xor_scalar[0], 9);
        EXPECT_EQ(xor_scalar[2], 15);
    }

    {
        int4 vec_lshift = make_int4(1, 2, 4, 8);
        const int4 lshifts = make_int4(3, 2, 1, 0);
        std::simd::where(selected, vec_lshift) <<= lshifts;
        EXPECT_EQ(vec_lshift[0], 8);
        EXPECT_EQ(vec_lshift[1], 2);
        EXPECT_EQ(vec_lshift[2], 8);
        EXPECT_EQ(vec_lshift[3], 8);
    }

    {
        int4 vec_rshift = make_int4(16, 8, 32, 64);
        const int4 rshifts = make_int4(2, 1, 3, 4);
        std::simd::where(selected, vec_rshift) >>= rshifts;
        EXPECT_EQ(vec_rshift[0], 4);
        EXPECT_EQ(vec_rshift[1], 8);
        EXPECT_EQ(vec_rshift[2], 4);
        EXPECT_EQ(vec_rshift[3], 64);
    }
}

TEST(SimdMemoryTest, WhereBoolOverloadAssignsAllOrNoLanes) {
    int4 values = make_int4(1, 2, 3, 4);
    const int4 other = make_int4(9, 8, 7, 6);

    std::simd::where(true, values) = other;
    EXPECT_EQ(values[0], 9);
    EXPECT_EQ(values[1], 8);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], 6);

    values = make_int4(1, 2, 3, 4);
    std::simd::where(false, values) = other;
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
    EXPECT_EQ(values[3], 4);
}

#if defined(FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)

TEST(SimdMemoryExtensionTest, UncheckedMaskedGatherAndScatterUseStandardOrder) {
    std::array<int, 8> input{{10, 11, 12, 13, 14, 15, 16, 17}};
    std::array<int, 8> output{{-1, -1, -1, -1, -1, -1, -1, -1}};
    const int4 indices = make_int4(6, 0, 3, 7);
    const mask4 selected(0b0101u);

    const auto gathered = std::simd::unchecked_gather_from<int4>(input.data(), selected, indices);
    EXPECT_EQ(gathered[0], 16);
    EXPECT_EQ(gathered[1], 0);
    EXPECT_EQ(gathered[2], 13);
    EXPECT_EQ(gathered[3], 0);

    std::simd::unchecked_scatter_to(gathered, output.data(), selected, indices);
    EXPECT_EQ(output[0], -1);
    EXPECT_EQ(output[3], 13);
    EXPECT_EQ(output[6], 16);
    EXPECT_EQ(output[7], -1);
}

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

TEST(SimdMemoryExtensionTest, UncheckedMaskedPointerOverloadsHonorMask) {
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

TEST(SimdMemoryDraftTest, UncheckedMemoryCoveragePending) {
    GTEST_SKIP() << "Enable FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS after unchecked_* stabilizes.";
}

#endif

} // namespace

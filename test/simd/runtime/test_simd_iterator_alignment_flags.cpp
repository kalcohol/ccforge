#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's iterator alignment flags."
#endif

namespace {

using vector4 = std::simd::vec<int, 4>;
using size_type = std::simd::simd_size_type;
using read_iterator = std::counted_iterator<const int*>;
using write_iterator = std::counted_iterator<int*>;
constexpr size_type count = 4;
constexpr std::size_t storage_alignment = 64;

static_assert(std::contiguous_iterator<read_iterator>);
static_assert(std::contiguous_iterator<write_iterator>);
static_assert(!std::is_pointer_v<read_iterator>);
static_assert(!std::is_pointer_v<write_iterator>);
static_assert(std::sized_sentinel_for<std::default_sentinel_t, read_iterator>);
static_assert(storage_alignment >= std::simd::alignment_v<vector4>);
static_assert(sizeof(std::simd::overaligned_flag<1>) > 0);
static_assert(sizeof(std::simd::overaligned_flag<64>) > 0);

template<class I>
concept can_load = requires(I first) {
    std::simd::partial_load<vector4>(first, count, std::simd::flag_aligned);
};

template<class I>
concept can_store = requires(const vector4& value, I first) {
    std::simd::partial_store(value, first, count, std::simd::flag_aligned);
};

static_assert(can_load<read_iterator>);
static_assert(can_store<write_iterator>);
static_assert(!can_store<read_iterator>);
static_assert(!can_load<std::reverse_iterator<int*>>);
static_assert(!can_store<std::reverse_iterator<int*>>);

// Instantiating the actual body distinguishes the former pointer-only guard;
// expression-only invocability checks cannot observe that static_assert.
constexpr bool constant_round_trip() {
    alignas(storage_alignment) const int input[4]{11, 22, 33, 44};
    alignas(storage_alignment) int output[4]{};
    const auto value = std::simd::partial_load<vector4>(
        read_iterator(input, count), count, std::simd::flag_aligned);
    std::simd::partial_store(value, write_iterator(output, count),
        std::default_sentinel, std::simd::flag_overaligned<storage_alignment>);
    return output[0] == 11 && output[1] == 22 && output[2] == 33 && output[3] == 44;
}

static_assert(constant_round_trip());

struct buffers {
    alignas(storage_alignment) std::array<int, 4> input{11, 22, 33, 44};
    alignas(storage_alignment) std::array<int, 4> output{-1, -1, -1, -1};

    read_iterator first() const { return read_iterator(input.data(), count); }
    write_iterator destination() { return write_iterator(output.data(), count); }

    bool is_aligned() const {
        return reinterpret_cast<std::uintptr_t>(input.data()) % storage_alignment == 0 &&
            reinterpret_cast<std::uintptr_t>(output.data()) % storage_alignment == 0;
    }
};

void expect_lanes(const vector4& value, const std::array<int, 4>& expected) {
    for (size_type lane = 0; lane < count; ++lane) {
        EXPECT_EQ(value[lane], expected[static_cast<std::size_t>(lane)]);
    }
}

auto selected_lanes() {
    return vector4::mask_type([](auto lane) {
        return decltype(lane)::value % 2 == 0;
    });
}

TEST(SimdIteratorAlignmentFlags, PartialCountMaskedAndUnmasked) {
    buffers data;
    ASSERT_TRUE(data.is_aligned());
    const auto flags = std::simd::flag_aligned;
    const auto value = std::simd::partial_load<vector4>(data.first(), count, flags);
    expect_lanes(value, data.input);
    std::simd::partial_store(value, data.destination(), count, flags);
    EXPECT_EQ(data.output, data.input);

    const auto mask = selected_lanes();
    const auto masked = std::simd::partial_load<vector4>(data.first(), count, mask, flags);
    expect_lanes(masked, {11, 0, 33, 0});
    data.output.fill(-1);
    std::simd::partial_store(value, data.destination(), count, mask, flags);
    EXPECT_EQ(data.output, (std::array<int, 4>{11, -1, 33, -1}));
}

TEST(SimdIteratorAlignmentFlags, PartialSizedSentinelMaskedAndUnmasked) {
    buffers data;
    ASSERT_TRUE(data.is_aligned());
    const auto flags = std::simd::flag_overaligned<storage_alignment>;
    const auto value = std::simd::partial_load<vector4>(data.first(), std::default_sentinel, flags);
    expect_lanes(value, data.input);
    std::simd::partial_store(value, data.destination(), std::default_sentinel, flags);
    EXPECT_EQ(data.output, data.input);

    const auto mask = selected_lanes();
    const auto masked = std::simd::partial_load<vector4>(
        data.first(), std::default_sentinel, mask, flags);
    expect_lanes(masked, {11, 0, 33, 0});
    data.output.fill(-1);
    std::simd::partial_store(value, data.destination(), std::default_sentinel, mask, flags);
    EXPECT_EQ(data.output, (std::array<int, 4>{11, -1, 33, -1}));
}

TEST(SimdIteratorAlignmentFlags, UncheckedCountAndSentinel) {
    buffers data;
    ASSERT_TRUE(data.is_aligned());
    const auto value = std::simd::unchecked_load<vector4>(
        data.first(), count, std::simd::flag_aligned);
    expect_lanes(value, data.input);
    std::simd::unchecked_store(value, data.destination(), count, std::simd::flag_aligned);
    EXPECT_EQ(data.output, data.input);

    const auto mask = selected_lanes();
    const auto flags = std::simd::flag_overaligned<storage_alignment>;
    const auto masked = std::simd::unchecked_load<vector4>(
        data.first(), std::default_sentinel, mask, flags);
    expect_lanes(masked, {11, 0, 33, 0});
    data.output.fill(-1);
    std::simd::unchecked_store(value, data.destination(), std::default_sentinel, mask, flags);
    EXPECT_EQ(data.output, (std::array<int, 4>{11, -1, 33, -1}));
}

TEST(SimdIteratorAlignmentFlags, GatherScatterShareTheContiguousIteratorRule) {
    buffers data;
    ASSERT_TRUE(data.is_aligned());
    const vector4 indices([](auto lane) {
        return 3 - static_cast<int>(decltype(lane)::value);
    });
    const auto value = std::simd::partial_gather_from<vector4>(
        data.first(), count, indices, std::simd::flag_aligned);
    expect_lanes(value, {44, 33, 22, 11});
    std::simd::partial_scatter_to(value, data.destination(), count,
        indices, std::simd::flag_aligned);
    EXPECT_EQ(data.output, data.input);

    const auto mask = selected_lanes();
    const auto flags = std::simd::flag_overaligned<storage_alignment>;
    const auto masked = std::simd::unchecked_gather_from<vector4>(data.first(), mask, indices, flags);
    expect_lanes(masked, {44, 0, 22, 0});
    data.output.fill(-1);
    std::simd::unchecked_scatter_to(value, data.destination(), mask, indices, flags);
    EXPECT_EQ(data.output, (std::array<int, 4>{-1, 22, -1, 44}));
}

TEST(SimdIteratorAlignmentFlags, VectorIteratorPointerAndDefaultFlagControls) {
    const std::vector<int> input{11, 22, 33, 44};
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(input.data()) % alignof(int), 0u);
    const auto from_vector = std::simd::partial_load<vector4>(
        input.cbegin(), count, std::simd::flag_overaligned<alignof(int)>);
    expect_lanes(from_vector, {11, 22, 33, 44});

    buffers data;
    ASSERT_TRUE(data.is_aligned());
    const auto from_pointer = std::simd::partial_load<vector4>(
        data.input.data(), count, std::simd::flag_aligned);
    const auto default_flags = std::simd::partial_load<vector4>(
        data.first(), count, std::simd::flag_default);
    expect_lanes(from_pointer, data.input);
    expect_lanes(default_flags, data.input);
}

TEST(SimdIteratorAlignmentFlags, ConvertingFlagsPreserveExactValues) {
    using float4 = std::simd::vec<float, 4>;
    static_assert(storage_alignment >= std::simd::alignment_v<float4, double>);
    alignas(storage_alignment) const std::array<double, 4> input{1.5, 2.5, 3.5, 4.5};
    alignas(storage_alignment) std::array<double, 4> output{};
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(input.data()) % storage_alignment, 0u);
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(output.data()) % storage_alignment, 0u);
    const auto flags = std::simd::flag_aligned | std::simd::flag_convert;
    const auto value = std::simd::partial_load<float4>(
        std::counted_iterator<const double*>(input.data(), count), count, flags);
    std::simd::partial_store(value,
        std::counted_iterator<double*>(output.data(), count), count, flags);
    for (size_type lane = 0; lane < count; ++lane) {
        EXPECT_EQ(value[lane], static_cast<float>(input[static_cast<std::size_t>(lane)]));
    }
    EXPECT_EQ(output, input);
}

} // namespace

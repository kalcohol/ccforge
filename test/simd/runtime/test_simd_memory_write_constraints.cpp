#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace {

using simd_test::int4;
using mask4 = int4::mask_type;

// Constness of a view handle does not make its referenced elements const.
struct mutable_proxy_view {
    int* storage;

    constexpr int* begin() const noexcept { return storage; }
    constexpr int* end() const noexcept { return storage + 8; }
    constexpr int* data() const noexcept { return storage; }
    constexpr std::size_t size() const noexcept { return 8; }
};

static_assert(std::ranges::contiguous_range<const mutable_proxy_view>);
static_assert(std::ranges::sized_range<const mutable_proxy_view>);
static_assert(std::indirectly_writable<
    std::ranges::iterator_t<const mutable_proxy_view>,
    std::ranges::range_value_t<const mutable_proxy_view>>);
static_assert(std::contiguous_iterator<std::counted_iterator<int*>>);
static_assert(std::contiguous_iterator<std::counted_iterator<const int*>>);

// Compare each overload separately so one rejection cannot hide an accepted sibling.
template<class R, bool Writable>
constexpr bool range_write_constraints() {
    return (requires(const int4& value, R&& output) {
        { std::simd::partial_store(value, std::forward<R>(output)) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const mask4& mask) {
        { std::simd::partial_store(value, std::forward<R>(output), mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output) {
        { std::simd::unchecked_store(value, std::forward<R>(output)) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const mask4& mask) {
        { std::simd::unchecked_store(value, std::forward<R>(output), mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const int4& indices) {
        { std::simd::partial_scatter_to(value, std::forward<R>(output), indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const mask4& mask, const int4& indices) {
        { std::simd::partial_scatter_to(value, std::forward<R>(output), mask, indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const int4& indices) {
        { std::simd::unchecked_scatter_to(value, std::forward<R>(output), indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, R&& output, const mask4& mask, const int4& indices) {
        { std::simd::unchecked_scatter_to(value, std::forward<R>(output), mask, indices) } -> std::same_as<void>;
    } == Writable);
}

static_assert(range_write_constraints<int (&)[8], true>());
static_assert(range_write_constraints<int (&&)[8], true>());
static_assert(range_write_constraints<const int (&)[8], false>());
static_assert(range_write_constraints<const int (&&)[8], false>());
static_assert(range_write_constraints<std::array<int, 8>&, true>());
static_assert(range_write_constraints<const std::array<int, 8>&, false>());
static_assert(range_write_constraints<std::array<int, 8>, true>());
static_assert(range_write_constraints<const std::array<int, 8>, false>());
static_assert(range_write_constraints<std::vector<int>&, true>());
static_assert(range_write_constraints<const std::vector<int>&, false>());
static_assert(range_write_constraints<std::span<int, 8>, true>());
static_assert(range_write_constraints<const std::span<int, 8>&, true>());
static_assert(range_write_constraints<std::span<int>&, true>());
static_assert(range_write_constraints<const std::span<int>&, true>());
static_assert(range_write_constraints<std::span<const int, 8>, false>());
static_assert(range_write_constraints<const std::span<const int, 8>&, false>());
static_assert(range_write_constraints<std::span<const int>&, false>());
static_assert(range_write_constraints<mutable_proxy_view, true>());
static_assert(range_write_constraints<const mutable_proxy_view&, true>());
static_assert(range_write_constraints<const std::ranges::subrange<int*>&, true>());
static_assert(range_write_constraints<const std::ranges::subrange<const int*>&, false>());

template<class I, bool Writable>
constexpr bool iterator_write_constraints() {
    return (requires(const int4& value, I&& output, std::simd::simd_size_type count) {
        { std::simd::partial_store(value, std::forward<I>(output), count) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, std::simd::simd_size_type count, const mask4& mask) {
        { std::simd::partial_store(value, std::forward<I>(output), count, mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, std::simd::simd_size_type count) {
        { std::simd::unchecked_store(value, std::forward<I>(output), count) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, std::simd::simd_size_type count, const mask4& mask) {
        { std::simd::unchecked_store(value, std::forward<I>(output), count, mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, std::simd::simd_size_type count, const int4& indices) {
        { std::simd::partial_scatter_to(value, std::forward<I>(output), count, indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, std::simd::simd_size_type count, const mask4& mask, const int4& indices) {
        { std::simd::partial_scatter_to(value, std::forward<I>(output), count, mask, indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, const int4& indices) {
        { std::simd::unchecked_scatter_to(value, std::forward<I>(output), indices) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I&& output, const mask4& mask, const int4& indices) {
        { std::simd::unchecked_scatter_to(value, std::forward<I>(output), mask, indices) } -> std::same_as<void>;
    } == Writable);
}

static_assert(iterator_write_constraints<int*, true>());
static_assert(iterator_write_constraints<int* const&, true>());
static_assert(iterator_write_constraints<const int*, false>());
static_assert(iterator_write_constraints<const int* const&, false>());
static_assert(iterator_write_constraints<std::vector<int>::iterator, true>());
static_assert(iterator_write_constraints<const std::vector<int>::iterator&, true>());
static_assert(iterator_write_constraints<std::vector<int>::const_iterator, false>());
static_assert(iterator_write_constraints<std::counted_iterator<int*>, true>());
static_assert(iterator_write_constraints<std::counted_iterator<const int*>, false>());

template<class I, class S, bool Writable>
constexpr bool sentinel_write_constraints() {
    return (requires(const int4& value, I first, S last) {
        { std::simd::partial_store(value, first, last) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I first, S last, const mask4& mask) {
        { std::simd::partial_store(value, first, last, mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I first, S last) {
        { std::simd::unchecked_store(value, first, last) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, I first, S last, const mask4& mask) {
        { std::simd::unchecked_store(value, first, last, mask) } -> std::same_as<void>;
    } == Writable);
}

static_assert(sentinel_write_constraints<int*, int*, true>());
static_assert(sentinel_write_constraints<const int*, const int*, false>());
static_assert(sentinel_write_constraints<std::vector<int>::iterator, std::vector<int>::iterator, true>());
static_assert(sentinel_write_constraints<std::vector<int>::const_iterator, std::vector<int>::const_iterator, false>());
static_assert(sentinel_write_constraints<std::counted_iterator<int*>, std::default_sentinel_t, true>());
static_assert(sentinel_write_constraints<std::counted_iterator<const int*>, std::default_sentinel_t, false>());

template<class P, bool Writable>
constexpr bool pointer_store_constraints() {
    return (requires(const int4& value, P output) {
        { std::simd::unchecked_store(value, output) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, P output, const mask4& mask) {
        { std::simd::unchecked_store(value, output, mask) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, P output, std::simd::simd_size_type count) {
        { std::simd::unchecked_store(value, output, count, std::simd::flag_default) } -> std::same_as<void>;
    } == Writable) && (requires(const int4& value, P output, std::simd::simd_size_type count, const mask4& mask) {
        { std::simd::unchecked_store(value, output, count, mask, std::simd::flag_default) } -> std::same_as<void>;
    } == Writable);
}

static_assert(pointer_store_constraints<int*, true>());
static_assert(pointer_store_constraints<const int*, false>());

template<class R>
concept readable_range_without_masked_unchecked_load = requires(R&& input, const int4& indices, const mask4& mask) {
    { std::simd::partial_load<int4>(std::forward<R>(input)) } -> std::same_as<int4>;
    { std::simd::partial_load<int4>(std::forward<R>(input), mask) } -> std::same_as<int4>;
    { std::simd::unchecked_load<int4>(std::forward<R>(input)) } -> std::same_as<int4>;
    { std::simd::partial_gather_from<int4>(std::forward<R>(input), indices) } -> std::same_as<int4>;
    { std::simd::partial_gather_from<int4>(std::forward<R>(input), mask, indices) } -> std::same_as<int4>;
    { std::simd::unchecked_gather_from<int4>(std::forward<R>(input), indices) } -> std::same_as<int4>;
    { std::simd::unchecked_gather_from<int4>(std::forward<R>(input), mask, indices) } -> std::same_as<int4>;
};

template<class R>
concept readable_range = readable_range_without_masked_unchecked_load<R> &&
    requires(R&& input, const mask4& mask) {
        { std::simd::unchecked_load<int4>(std::forward<R>(input), mask) } -> std::same_as<int4>;
    };

// R-masked-array-unchecked-load: existing pointer/range overload competition.
static_assert(readable_range_without_masked_unchecked_load<const int (&)[8]>);
static_assert(readable_range<const std::array<int, 8>&>);
static_assert(readable_range<const std::vector<int>&>);
static_assert(readable_range<const std::span<const int, 8>&>);
static_assert(readable_range<const std::span<int, 8>&>);
static_assert(readable_range<const mutable_proxy_view&>);

constexpr int4 values() {
    return int4([](auto lane) { return 10 * (static_cast<int>(lane) + 1); });
}

constexpr int4 scatter_indices() {
    return int4([](auto lane) {
        constexpr int offsets[]{6, 0, 3, 7};
        return offsets[static_cast<std::size_t>(lane)];
    });
}

constexpr bool writable_views_are_constexpr() {
    std::array<int, 8> output{};
    const mutable_proxy_view view{output.data()};
    const mask4 selected(std::bitset<4>(0b0101));
    std::simd::partial_store(values(), view, selected);
    if (output != std::array<int, 8>{10, 0, 30, 0, 0, 0, 0, 0}) {
        return false;
    }
    output.fill(0);
    std::simd::unchecked_scatter_to(values(), view, selected, scatter_indices());
    return output == std::array<int, 8>{0, 0, 0, 30, 0, 0, 10, 0};
}

static_assert(writable_views_are_constexpr());

constexpr std::array<int, 8> stored{10, 20, 30, 40, -1, -1, -1, -1};
constexpr std::array<int, 8> masked_stored{10, -1, 30, -1, -1, -1, -1, -1};
constexpr std::array<int, 8> scattered{20, -1, -1, 30, -1, -1, 10, 40};
constexpr std::array<int, 8> masked_scattered{-1, -1, -1, 30, -1, -1, 10, -1};

template<class Write>
void check_write(int* output, Write write, const std::array<int, 8>& expected) {
    std::fill_n(output, 8, -1);
    write();
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(output[i], expected[i]) << "element " << i;
    }
}

template<class R>
void check_range_writes(R&& output) {
    const int4 value = values();
    const int4 indices = scatter_indices();
    const mask4 selected(std::bitset<4>(0b0101));
    int* data = std::ranges::data(output);

    check_write(data, [&] { std::simd::partial_store(value, std::forward<R>(output)); }, stored);
    check_write(data, [&] { std::simd::partial_store(value, std::forward<R>(output), selected); }, masked_stored);
    check_write(data, [&] { std::simd::unchecked_store(value, std::forward<R>(output)); }, stored);
    check_write(data, [&] { std::simd::unchecked_store(value, std::forward<R>(output), selected); }, masked_stored);
    check_write(data, [&] { std::simd::partial_scatter_to(value, std::forward<R>(output), indices); }, scattered);
    check_write(data, [&] { std::simd::partial_scatter_to(value, std::forward<R>(output), selected, indices); }, masked_scattered);
    check_write(data, [&] { std::simd::unchecked_scatter_to(value, std::forward<R>(output), indices); }, scattered);
    check_write(data, [&] { std::simd::unchecked_scatter_to(value, std::forward<R>(output), selected, indices); }, masked_scattered);
}

template<class I>
void check_iterator_writes(I first) {
    const int4 value = values();
    const int4 indices = scatter_indices();
    const mask4 selected(std::bitset<4>(0b0101));
    int* data = std::to_address(first);

    check_write(data, [&] { std::simd::partial_store(value, first, 8); }, stored);
    check_write(data, [&] { std::simd::partial_store(value, first, 8, selected); }, masked_stored);
    check_write(data, [&] { std::simd::unchecked_store(value, first, 8); }, stored);
    check_write(data, [&] { std::simd::unchecked_store(value, first, 8, selected); }, masked_stored);
    check_write(data, [&] { std::simd::partial_scatter_to(value, first, 8, indices); }, scattered);
    check_write(data, [&] { std::simd::partial_scatter_to(value, first, 8, selected, indices); }, masked_scattered);
    check_write(data, [&] { std::simd::unchecked_scatter_to(value, first, indices); }, scattered);
    check_write(data, [&] { std::simd::unchecked_scatter_to(value, first, selected, indices); }, masked_scattered);
}

template<class I, class S>
void check_sentinel_writes(I first, S last) {
    const int4 value = values();
    const mask4 selected(std::bitset<4>(0b0101));
    int* data = std::to_address(first);

    check_write(data, [&] { std::simd::partial_store(value, first, last); }, stored);
    check_write(data, [&] { std::simd::partial_store(value, first, last, selected); }, masked_stored);
    check_write(data, [&] { std::simd::unchecked_store(value, first, last); }, stored);
    check_write(data, [&] { std::simd::unchecked_store(value, first, last, selected); }, masked_stored);
}

TEST(SimdMemoryWriteConstraintsTest, MutableRangesRetainAllWriteForms) {
    int array[8]{};
    std::array<int, 8> standard_array{};
    std::vector<int> vector(8);

    check_range_writes(array);
    check_range_writes(std::move(array));
    check_range_writes(standard_array);
    check_range_writes(vector);
    check_range_writes(std::span<int, 8>(standard_array));
    check_range_writes(std::span<int>(vector));
}

TEST(SimdMemoryWriteConstraintsTest, ConstMutableProxyHandlesStillWriteElements) {
    std::array<int, 8> output{};
    const std::span<int, 8> span(output);
    const mutable_proxy_view proxy{output.data()};
    const std::ranges::subrange range(output.data(), output.data() + output.size());

    check_range_writes(span);
    check_range_writes(proxy);
    check_range_writes(range);
}

TEST(SimdMemoryWriteConstraintsTest, ContiguousIteratorsRetainCountAndSentinelForms) {
    std::array<int, 8> output{};
    std::vector<int> vector(8);

    check_iterator_writes(output.data());
    check_iterator_writes(vector.begin());
    check_iterator_writes(std::counted_iterator(output.data(), 8));
    check_sentinel_writes(output.data(), output.data() + output.size());
    check_sentinel_writes(vector.begin(), vector.end());
    check_sentinel_writes(std::counted_iterator(output.data(), 8), std::default_sentinel);
}

TEST(SimdMemoryWriteConstraintsTest, PointerStoreExtensionsRetainWritableControls) {
    std::array<int, 8> output{};
    int* const pointer = output.data();
    const int4 value = values();
    const mask4 selected(std::bitset<4>(0b0101));

    check_write(pointer, [&] { std::simd::unchecked_store(value, pointer); }, stored);
    check_write(pointer, [&] { std::simd::unchecked_store(value, pointer, selected); }, masked_stored);
    check_write(pointer, [&] { std::simd::unchecked_store(value, pointer, 8, std::simd::flag_default); }, stored);
    check_write(pointer, [&] { std::simd::unchecked_store(value, pointer, 8, selected, std::simd::flag_default); }, masked_stored);
}

TEST(SimdMemoryWriteConstraintsTest, FlagConvertStillControlsWritableNarrowingTargets) {
    std::array<short, 8> output{};
    const std::span<short, 8> view(output);
    const int4 value = values();
    const int4 indices = scatter_indices();
    const mask4 selected(std::bitset<4>(0b0101));

    std::simd::partial_store(value, view, std::simd::flag_convert);
    for (std::size_t i = 0; i < output.size(); ++i) {
        EXPECT_EQ(output[i], i < 4 ? stored[i] : 0);
    }
    output.fill(0);
    std::simd::unchecked_scatter_to(value, view, selected, indices, std::simd::flag_convert);
    EXPECT_EQ(output, (std::array<short, 8>{0, 0, 0, 30, 0, 0, 10, 0}));
}

} // namespace

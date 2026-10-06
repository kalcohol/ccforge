#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace simd_test;

template<class R>
concept has_explicit_gather_forms = requires(
    R&& input, const int4& indices, const typename int4::mask_type& selected) {
    { std::simd::unchecked_gather_from<int4>(std::forward<R>(input), indices) } -> std::same_as<int4>;
    { std::simd::unchecked_gather_from<int4>(std::forward<R>(input), selected, indices) } -> std::same_as<int4>;
};

template<class R>
concept has_range_gather_forms = has_explicit_gather_forms<R> && requires(
    R&& input, const int4& indices, const typename int4::mask_type& selected) {
    { std::simd::unchecked_gather_from(std::forward<R>(input), indices) } -> std::same_as<int4>;
    { std::simd::unchecked_gather_from(std::forward<R>(input), selected, indices) } -> std::same_as<int4>;
};

static_assert(has_range_gather_forms<int (&)[8]>);
static_assert(has_range_gather_forms<const int (&)[8]>);
static_assert(has_range_gather_forms<int (&&)[8]>);
static_assert(has_range_gather_forms<const int (&&)[8]>);
static_assert(has_range_gather_forms<std::array<int, 8>&>);
static_assert(has_range_gather_forms<const std::array<int, 8>&>);
static_assert(has_range_gather_forms<std::span<int, 8>>);
static_assert(has_range_gather_forms<std::span<const int, 8>>);

template<class R>
concept has_scatter_forms = requires(
    const int4& value, R&& output, const int4& indices, const typename int4::mask_type& selected) {
    { std::simd::unchecked_scatter_to(value, std::forward<R>(output), indices) } -> std::same_as<void>;
    { std::simd::unchecked_scatter_to(value, std::forward<R>(output), selected, indices) } -> std::same_as<void>;
};

template<class R>
constexpr bool rejects_mismatched_scatter_width() {
    return !requires(const int8& value, R&& output, const int4& indices) {
        std::simd::unchecked_scatter_to(value, std::forward<R>(output), indices);
    } && !requires(const int8& value, R&& output, const int4& indices,
                  const typename int4::mask_type& selected) {
        std::simd::unchecked_scatter_to(value, std::forward<R>(output), selected, indices);
    };
}

static_assert(has_scatter_forms<int (&)[8]>);
static_assert(has_scatter_forms<int (&&)[8]>);
static_assert(has_scatter_forms<std::array<int, 8>&>);
static_assert(has_scatter_forms<std::span<int, 8>>);
static_assert(has_scatter_forms<const std::span<int, 8>&>);
static_assert(has_scatter_forms<std::span<int>>);
static_assert(rejects_mismatched_scatter_width<int (&)[8]>());
static_assert(rejects_mismatched_scatter_width<std::span<int, 8>>());

constexpr int4 gather_indices() {
    return int4([](auto lane) {
        constexpr int offsets[]{6, 0, 3, 7};
        return offsets[static_cast<std::size_t>(lane)];
    });
}

constexpr int4 scatter_values() {
    return int4([](auto lane) {
        return 10 * (static_cast<int>(lane) + 1);
    });
}

constexpr bool gather_arrays_are_constexpr() {
    int input[]{10, 11, 12, 13, 14, 15, 16, 17};
    const int const_input[]{10, 11, 12, 13, 14, 15, 16, 17};
    const int4 indices = gather_indices();
    const auto mutable_result = std::simd::unchecked_gather_from<int4>(input, indices);
    const auto const_result = std::simd::unchecked_gather_from(const_input, indices);
    return mutable_result[0] == 16 && mutable_result[1] == 10 &&
        mutable_result[2] == 13 && mutable_result[3] == 17 &&
        const_result[0] == 16 && const_result[1] == 10 &&
        const_result[2] == 13 && const_result[3] == 17;
}

static_assert(gather_arrays_are_constexpr());

constexpr bool scatter_arrays_are_constexpr() {
    int output[]{-1, -1, -1, -1, -1, -1, -1, -1};
    std::simd::unchecked_scatter_to(scatter_values(), output, gather_indices());
    return output[0] == 20 && output[1] == -1 && output[2] == -1 && output[3] == 30 &&
        output[4] == -1 && output[5] == -1 && output[6] == 10 && output[7] == 40;
}

static_assert(scatter_arrays_are_constexpr());

template<class V, class T>
void expect_lanes(const V& result, const std::array<T, 4>& expected) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(result[static_cast<std::simd::simd_size_type>(i)], expected[i]);
    }
}

template<class R>
void check_range_gather(R&& input) {
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    const std::array<int, 4> all_expected{16, 10, 13, 17};
    const std::array<int, 4> masked_expected{16, 0, 13, 0};

    expect_lanes(std::simd::unchecked_gather_from<int4>(std::forward<R>(input), indices), all_expected);
    expect_lanes(std::simd::unchecked_gather_from(std::forward<R>(input), indices), all_expected);
    expect_lanes(std::simd::unchecked_gather_from<int4>(std::forward<R>(input), selected, indices), masked_expected);
    expect_lanes(std::simd::unchecked_gather_from(std::forward<R>(input), selected, indices), masked_expected);
}

TEST(SimdMemoryArraysTest, GatherBuiltInArraysUseAllRangeForms) {
    int input[]{10, 11, 12, 13, 14, 15, 16, 17};
    const int const_input[]{10, 11, 12, 13, 14, 15, 16, 17};

    check_range_gather(input);
    check_range_gather(const_input);
    check_range_gather(std::move(input));
    check_range_gather(std::move(const_input));
}

TEST(SimdMemoryArraysTest, GatherArrayAndSpanControlsRemainUnambiguous) {
    std::array<int, 8> input{10, 11, 12, 13, 14, 15, 16, 17};
    const std::array<int, 8> const_input{10, 11, 12, 13, 14, 15, 16, 17};

    check_range_gather(input);
    check_range_gather(const_input);
    check_range_gather(std::span<int, 8>(input));
    check_range_gather(std::span<const int, 8>(const_input));
}

TEST(SimdMemoryArraysTest, GatherMaskedArraysIgnoreUnselectedInvalidOffsets) {
    int input[]{10, 11, 12, 13, 14, 15, 16, 17};
    const int const_input[]{10, 11, 12, 13, 14, 15, 16, 17};
    const int4 indices([](auto lane) {
        constexpr int offsets[]{1, -7, 5, -9};
        return offsets[static_cast<std::size_t>(lane)];
    });
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    const std::array<int, 4> expected{11, 0, 15, 0};

    expect_lanes(std::simd::unchecked_gather_from<int4>(input, selected, indices), expected);
    expect_lanes(std::simd::unchecked_gather_from(input, selected, indices), expected);
    expect_lanes(std::simd::unchecked_gather_from<int4>(const_input, selected, indices), expected);
    expect_lanes(std::simd::unchecked_gather_from(const_input, selected, indices), expected);
}

TEST(SimdMemoryArraysTest, GatherArrayConversionsAndAlignmentFlagsArePreserved) {
    const float input[]{10.75f, 11.25f, 12.5f, 13.75f, 14.25f, 15.5f, 16.75f, 17.25f};
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    const auto deduced = std::simd::unchecked_gather_from(input, indices);
    static_assert(std::is_same_v<std::remove_cv_t<decltype(deduced)>, std::simd::vec<float, 4>>);
    expect_lanes(deduced, std::array<float, 4>{16.75f, 10.75f, 13.75f, 17.25f});
    expect_lanes(std::simd::unchecked_gather_from<int4>(input, indices, std::simd::flag_convert),
        std::array<int, 4>{16, 10, 13, 17});
    expect_lanes(std::simd::unchecked_gather_from<int4>(input, selected, indices, std::simd::flag_convert),
        std::array<int, 4>{16, 0, 13, 0});

    alignas(64) const int aligned_input[]{10, 11, 12, 13, 14, 15, 16, 17};
    expect_lanes(std::simd::unchecked_gather_from<int4>(aligned_input, indices, std::simd::flag_overaligned<64>),
        std::array<int, 4>{16, 10, 13, 17});
    expect_lanes(std::simd::unchecked_gather_from<int4>(aligned_input, selected, indices, std::simd::flag_overaligned<64>),
        std::array<int, 4>{16, 0, 13, 0});
}

template<class T>
void expect_memory(const T* first, const std::array<T, 8>& expected) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(first[i], expected[i]);
    }
}

template<class R>
void check_scatter(R&& output, int* first) {
    const int4 values = scatter_values();
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));

    std::fill_n(first, 8, -1);
    std::simd::unchecked_scatter_to(values, std::forward<R>(output), indices);
    expect_memory(first, std::array<int, 8>{20, -1, -1, 30, -1, -1, 10, 40});
    std::fill_n(first, 8, -1);
    std::simd::unchecked_scatter_to(values, std::forward<R>(output), selected, indices);
    expect_memory(first, std::array<int, 8>{-1, -1, -1, 30, -1, -1, 10, -1});
}

TEST(SimdMemoryArraysTest, ScatterBuiltInArraysUseBothRangeForms) {
    int output[8]{};
    check_scatter(output, output);
    check_scatter(std::move(output), output);
}

TEST(SimdMemoryArraysTest, ScatterArrayAndSpanControlsRemainUnambiguous) {
    std::array<int, 8> output{};
    std::span<int, 8> view(output);
    const std::span<int, 8> const_view(output);

    check_scatter(output, output.data());
    check_scatter(view, output.data());
    check_scatter(const_view, output.data());
    check_scatter(std::span<int, 8>(output), output.data());
    check_scatter(std::span<int>(output), output.data());
}

TEST(SimdMemoryArraysTest, ScatterMaskedArrayAndSpanIgnoreUnselectedInvalidOffsets) {
    int output[]{-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, 8> span_output{-1, -1, -1, -1, -1, -1, -1, -1};
    const int4 indices([](auto lane) {
        constexpr int offsets[]{1, -7, 5, -9};
        return offsets[static_cast<std::size_t>(lane)];
    });
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    const std::array<int, 8> expected{-1, 10, -1, -1, -1, 30, -1, -1};

    std::simd::unchecked_scatter_to(scatter_values(), output, selected, indices);
    expect_memory(output, expected);
    std::simd::unchecked_scatter_to(scatter_values(), std::span<int, 8>(span_output), selected, indices);
    expect_memory(span_output.data(), expected);
}

TEST(SimdMemoryArraysTest, ScatterArrayConversionsAndAlignmentFlagsArePreserved) {
    const float4 values([](auto lane) {
        return 10.75f * (static_cast<float>(lane) + 1.0f);
    });
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    int output[8]{};

    std::fill_n(output, 8, -1);
    std::simd::unchecked_scatter_to(values, output, indices, std::simd::flag_convert);
    expect_memory(output, std::array<int, 8>{21, -1, -1, 32, -1, -1, 10, 43});
    std::fill_n(output, 8, -1);
    std::simd::unchecked_scatter_to(values, output, selected, indices, std::simd::flag_convert);
    expect_memory(output, std::array<int, 8>{-1, -1, -1, 32, -1, -1, 10, -1});

    alignas(64) int aligned_output[8]{};
    std::fill_n(aligned_output, 8, -1);
    std::simd::unchecked_scatter_to(scatter_values(), aligned_output, indices, std::simd::flag_overaligned<64>);
    expect_memory(aligned_output, std::array<int, 8>{20, -1, -1, 30, -1, -1, 10, 40});
    std::fill_n(aligned_output, 8, -1);
    std::simd::unchecked_scatter_to(scatter_values(), aligned_output, selected, indices, std::simd::flag_overaligned<64>);
    expect_memory(aligned_output, std::array<int, 8>{-1, -1, -1, 30, -1, -1, 10, -1});
}

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)

class hybrid_iterator {
public:
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using reference = int&;
    using pointer = int*;
    using iterator_concept = std::contiguous_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;

    hybrid_iterator() = default;
    hybrid_iterator(int* position, int* last, int* copies) noexcept
        : position_(position), last_(last), copies_(copies) {}
    hybrid_iterator(const hybrid_iterator& other) noexcept
        : position_(other.position_), last_(other.last_), copies_(other.copies_) {
        if (copies_) {
            ++*copies_;
        }
    }
    hybrid_iterator& operator=(const hybrid_iterator&) = default;

    int& operator*() const noexcept { return *position_; }
    int* operator->() const noexcept { return position_; }
    int& operator[](difference_type offset) const noexcept { return position_[offset]; }
    hybrid_iterator& operator++() noexcept { ++position_; return *this; }
    hybrid_iterator& operator--() noexcept { --position_; return *this; }
    hybrid_iterator operator++(int) noexcept {
        auto previous = *this;
        ++*this;
        return previous;
    }
    hybrid_iterator operator--(int) noexcept {
        auto previous = *this;
        --*this;
        return previous;
    }
    hybrid_iterator& operator+=(difference_type offset) noexcept { position_ += offset; return *this; }
    hybrid_iterator& operator-=(difference_type offset) noexcept { position_ -= offset; return *this; }

    friend hybrid_iterator operator+(const hybrid_iterator& first, difference_type offset) noexcept {
        return hybrid_iterator(first.position_ + offset, first.last_, first.copies_);
    }
    friend hybrid_iterator operator+(difference_type offset, const hybrid_iterator& first) noexcept {
        return first + offset;
    }
    friend hybrid_iterator operator-(const hybrid_iterator& first, difference_type offset) noexcept {
        return hybrid_iterator(first.position_ - offset, first.last_, first.copies_);
    }
    friend difference_type operator-(const hybrid_iterator& left, const hybrid_iterator& right) noexcept {
        return left.position_ == right.position_ ? 0 : left.position_ - right.position_;
    }
    friend bool operator==(const hybrid_iterator& left, const hybrid_iterator& right) noexcept {
        return left.position_ == right.position_;
    }
    friend auto operator<=>(const hybrid_iterator& left, const hybrid_iterator& right) noexcept {
        return std::compare_three_way{}(left.position_, right.position_);
    }

    // A const iterator remains usable, but range access is mutable-only.
    hybrid_iterator begin() noexcept { return hybrid_iterator(position_, last_, copies_); }
    hybrid_iterator end() noexcept { return hybrid_iterator(last_, last_, copies_); }
    int* data() noexcept { return position_; }
    std::size_t size() noexcept {
        return position_ == last_ ? 0 : static_cast<std::size_t>(last_ - position_);
    }

private:
    int* position_ = nullptr;
    int* last_ = nullptr;
    int* copies_ = nullptr;
};

static_assert(std::copyable<hybrid_iterator>);
static_assert(std::contiguous_iterator<hybrid_iterator>);
static_assert(std::ranges::contiguous_range<hybrid_iterator>);
static_assert(std::ranges::sized_range<hybrid_iterator>);
static_assert(!std::ranges::range<const hybrid_iterator>);
static_assert(std::simd::detail::is_contiguous_load_store_range<hybrid_iterator>::value);
static_assert(!std::simd::detail::is_contiguous_load_store_range<const hybrid_iterator&>::value);
static_assert(has_explicit_gather_forms<const hybrid_iterator&>);
static_assert(has_scatter_forms<const hybrid_iterator&>);

static_assert(has_explicit_gather_forms<int*>);
static_assert(has_explicit_gather_forms<int*&>);
static_assert(has_explicit_gather_forms<const int* const&>);
static_assert(has_explicit_gather_forms<const int (&)[]>);
static_assert(has_explicit_gather_forms<int_iter&>);
static_assert(has_explicit_gather_forms<const const_int_iter&>);
static_assert(has_explicit_gather_forms<int_iter>);
static_assert(has_scatter_forms<int*>);
static_assert(has_scatter_forms<int*&>);
static_assert(has_scatter_forms<int* const&>);
static_assert(has_scatter_forms<int (&)[]>);
static_assert(has_scatter_forms<int_iter&>);
static_assert(has_scatter_forms<const int_iter&>);
static_assert(has_scatter_forms<int_iter>);
static_assert(rejects_mismatched_scatter_width<int*&>());
static_assert(rejects_mismatched_scatter_width<int_iter&>());

template<class I>
void check_iterator_gather(I&& first) {
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));
    expect_lanes(std::simd::unchecked_gather_from<int4>(std::forward<I>(first), indices),
        std::array<int, 4>{16, 10, 13, 17});
    expect_lanes(std::simd::unchecked_gather_from<int4>(std::forward<I>(first), selected, indices),
        std::array<int, 4>{16, 0, 13, 0});
}

TEST(SimdMemoryArraysTest, GatherPointerAndIteratorExtensionsKeepCvrefSupport) {
    int input[]{10, 11, 12, 13, 14, 15, 16, 17};
    int* first = input;
    const int* const const_first = input;
    const int (&unknown_bound)[] = input;
    check_iterator_gather(first);
    check_iterator_gather(const_first);
    check_iterator_gather(input + 0);
    check_iterator_gather(unknown_bound);

    std::vector<int> storage{10, 11, 12, 13, 14, 15, 16, 17};
    auto iterator = storage.begin();
    const auto const_iterator = storage.cbegin();
    check_iterator_gather(iterator);
    check_iterator_gather(const_iterator);
    check_iterator_gather(storage.begin());
    EXPECT_EQ(iterator, storage.begin());
    EXPECT_EQ(const_iterator, storage.cbegin());
}

TEST(SimdMemoryArraysTest, ScatterPointerAndIteratorExtensionsKeepCvrefSupport) {
    int output[8]{};
    int* first = output;
    int* const const_first = output;
    int (&unknown_bound)[] = output;
    check_scatter(first, output);
    check_scatter(const_first, output);
    check_scatter(output + 0, output);
    check_scatter(unknown_bound, output);

    std::vector<int> storage(8);
    auto iterator = storage.begin();
    const auto const_iterator = storage.begin();
    check_scatter(iterator, storage.data());
    check_scatter(const_iterator, storage.data());
    check_scatter(storage.begin(), storage.data());
    EXPECT_EQ(iterator, storage.begin());
    EXPECT_EQ(const_iterator, storage.begin());
}

TEST(SimdMemoryArraysTest, ConstHybridIteratorRetainsByValueGatherAndScatter) {
    int input[]{10, 11, 12, 13, 14, 15, 16, 17};
    int output[8]{};
    int copies = 0;
    const hybrid_iterator first(input, input + 8, std::addressof(copies));
    const hybrid_iterator destination(output, output + 8, std::addressof(copies));
    const int4 indices = gather_indices();
    const typename int4::mask_type selected(std::bitset<4>(0b0101));

    copies = 0;
    const auto gathered = std::simd::unchecked_gather_from<int4>(first, indices);
    EXPECT_EQ(copies, 1);
    expect_lanes(gathered, std::array<int, 4>{16, 10, 13, 17});
    EXPECT_EQ(std::to_address(first), input);

    copies = 0;
    const auto masked = std::simd::unchecked_gather_from<int4>(first, selected, indices);
    EXPECT_EQ(copies, 1);
    expect_lanes(masked, std::array<int, 4>{16, 0, 13, 0});
    EXPECT_EQ(std::to_address(first), input);

    std::fill_n(output, 8, -1);
    copies = 0;
    std::simd::unchecked_scatter_to(scatter_values(), destination, indices);
    EXPECT_EQ(copies, 1);
    expect_memory(output, std::array<int, 8>{20, -1, -1, 30, -1, -1, 10, 40});
    EXPECT_EQ(std::to_address(destination), output);

    std::fill_n(output, 8, -1);
    copies = 0;
    std::simd::unchecked_scatter_to(scatter_values(), destination, selected, indices);
    EXPECT_EQ(copies, 1);
    expect_memory(output, std::array<int, 8>{-1, -1, -1, 30, -1, -1, 10, -1});
    EXPECT_EQ(std::to_address(destination), output);
}

#endif

} // namespace
